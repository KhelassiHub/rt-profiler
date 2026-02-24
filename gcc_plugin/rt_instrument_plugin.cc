/*
 * rt_instrument_plugin.cc
 *
 * A GCC plugin that instruments basic blocks (BBs) of a target function with
 * cycle-counter hooks, enabling per-BB PMU profiling.
 *
 * Two operating modes, selected by the arguments passed to gcc -fplugin-arg-...:
 *
 *   Map-gen mode  (no -bbid arg):
 *     Visits every BB in target_func and writes bb_map.json describing them.
 *     No code is modified in the compiled program.
 *
 *   Instrument mode  (-bbid=N):
 *     Inserts __rt_bb_start(N, file, line) at the top of BB N and
 *     __rt_bb_end(N) just before its terminator (branch/return).
 *     The runtime (rt_record.c) measures the cycle delta between these calls.
 *
 * Plugin arguments (passed as -fplugin-arg-rt_instrument_plugin-KEY=VALUE):
 *   bbmap=PATH   path to write/read bb_map.json   (default: ./bb_map.json)
 *   bbid=N       sequential BB index to instrument (default: none = map-gen)
 *   func=NAME    function to target               (default: main)
 */

/* ── GCC plugin headers (mandatory, order matters) ── */
#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"              /* GCC's type/declaration representation (tree nodes) */
#include "gimple.h"            /* GIMPLE IR: the intermediate representation we operate on */
#include "gimple-iterator.h"   /* iterators to walk/insert statements inside a BB */
#include "context.h"           /* gcc::context — the global GCC compiler instance */
#include "function.h"          /* the 'function' struct representing a compiled function */
#include "basic-block.h"       /* basic_block type and FOR_EACH_BB_FN macro */
#include "tree-pass.h"         /* pass registration API */

#include <string.h>
#include <stdio.h>

/* Required by GCC: declares the plugin is GPL-compatible (mandatory symbol). */
int plugin_is_GPL_compatible;

/* ── Plugin state ─────────────────────────────────────────────────────────── */

/* GCC tree nodes representing the two runtime hook function declarations.
 * Built once in ensure_bb_decls() and reused for every call insertion. */
static tree bb_start_decl = nullptr;
static tree bb_end_decl   = nullptr;

static const char* bbmap_path  = "./bb_map.json"; /* where to write the BB map */
static int         target_bbid = -1;              /* -1 = map-gen, >= 0 = instrument */
static const char* target_func = "main";          /* only process this function */
static FILE*       bbmap_file  = nullptr;         /* open JSON file handle */
static int         bbmap_count = 0;               /* entries written so far (for commas) */

/* ── Hook declarations ────────────────────────────────────────────────────── */

/*
 * Tell GCC about the two runtime functions it will call.
 * We do not have their source — they live in rt_record.c — so we declare them
 * as external symbols, exactly like a forward declaration in C:
 *   void __rt_bb_start(int id, char* file, int line);
 *   void __rt_bb_end(int id);
 *
 * The GCC tree API requires building the type signature manually using
 * tree_cons (a linked list of parameter types). This verbosity is
 * unavoidable — it is the GCC internal API for constructing function types.
 */
static void ensure_bb_decls() {
  if (bb_start_decl) return;   /* already built */

  tree vt = void_type_node;                      /* return type: void   */
  tree it = integer_type_node;                   /* param type:  int    */
  tree ct = build_pointer_type(char_type_node);  /* param type:  char*  */

  /* Build: void __rt_bb_start(int, char*, int) */
  bb_start_decl = build_fn_decl("__rt_bb_start",
    build_function_type(vt, tree_cons(NULL_TREE, it,
                            tree_cons(NULL_TREE, ct,
                            tree_cons(NULL_TREE, it, NULL_TREE)))));
  DECL_EXTERNAL(bb_start_decl) = 1;  /* defined elsewhere (rt_record.o) */
  TREE_PUBLIC(bb_start_decl)   = 1;

  /* Build: void __rt_bb_end(int) */
  bb_end_decl = build_fn_decl("__rt_bb_end",
    build_function_type(vt, tree_cons(NULL_TREE, it, NULL_TREE)));
  DECL_EXTERNAL(bb_end_decl) = 1;
  TREE_PUBLIC(bb_end_decl)   = 1;
}

/* ── Argument parsing ─────────────────────────────────────────────────────── */

/* Reads -fplugin-arg-rt_instrument_plugin-KEY=VALUE flags passed to gcc. */
static void parse_args(struct plugin_name_args* info) {
  for (int i = 0; i < info->argc; ++i) {
    const char* k = info->argv[i].key, *v = info->argv[i].value;
    if (!k || !v) continue;
    if      (!strcmp(k, "bbmap")) bbmap_path  = v;
    else if (!strcmp(k, "bbid"))  target_bbid = atoi(v);
    else if (!strcmp(k, "func"))  target_func = v;
  }
}

/* ── Map file finalization ────────────────────────────────────────────────── */

/*
 * Called by GCC at the end of the translation unit (after all functions have
 * been processed). Closes the JSON array and the file.
 */
static void finish_bb_map(void*, void*) {
  if (!bbmap_file) return;
  fprintf(bbmap_file, "\n]\n");
  fclose(bbmap_file);
  bbmap_file = nullptr;
}

/* ── Core pass: visit every BB in the target function ────────────────────── */

/*
 * This function is called by GCC once per compiled function.
 * It operates on the GIMPLE IR — a simplified, compiler-internal
 * representation of the source code, already lowered from the AST.
 *
 * For each BB in target_func it:
 *   1. Finds the source location of the first real statement.
 *   2. Writes a JSON entry to bb_map.json (map-gen mode).
 *   3. Inserts __rt_bb_start / __rt_bb_end calls (instrument mode).
 */
static void instrument_basic_blocks(function* fn) {
  /* Safety checks: function must have a CFG and a name. */
  if (!fn || !fn->cfg || !fn->decl || !DECL_NAME(fn->decl)) return;

  /* Get the function name and skip it if it is not our target. */
  const char* fname = IDENTIFIER_POINTER(DECL_NAME(fn->decl));
  if (!fname || strcmp(fname, target_func) != 0) return;

  ensure_bb_decls();

  /* Open bb_map.json once, only in map-gen mode. */
  if (target_bbid < 0 && !bbmap_file) {
    bbmap_file = fopen(bbmap_path, "w");
    if (bbmap_file) fprintf(bbmap_file, "[");
  }

  basic_block bb;
  int seq = 0;  /* our stable BB index: 0, 1, 2, ... in CFG traversal order */

  FOR_EACH_BB_FN(bb, fn) {
    /* gsi = iterator pointing to the first statement inside this BB. */
    gimple_stmt_iterator gsi = gsi_start_bb(bb);
    if (gsi_end_p(gsi)) { ++seq; continue; }  /* empty BB — skip */

    /* Resolve the source file/line of the first statement. */
    expanded_location loc = expand_location(gimple_location(gsi_stmt(gsi)));

    /* Skip synthetic BBs GCC adds with no corresponding source line
     * (e.g. the implicit return epilogue appended to main). */
    if (!loc.file || loc.line <= 0) { ++seq; continue; }

    const char* file = loc.file;
    int         line = loc.line;

    /* ── Map-gen mode: write one JSON entry per BB ── */
    if (bbmap_file) {
      if (bbmap_count++) fprintf(bbmap_file, ",");
      fprintf(bbmap_file,
        "\n  {\"id\":%d,\"seq\":%d,\"function\":\"%s\",\"file\":\"%s\",\"line\":%d}",
        seq, seq, fname, file, line);
    }

    /* ── Instrument mode: insert hooks around the target BB only ── */
    if (target_bbid >= 0 && seq == target_bbid) {
      /* Build the constant arguments we will pass to the hook calls. */
      tree id_cst   = build_int_cst(integer_type_node, seq);
      tree file_str = build_string_literal(strlen(file) + 1, file);
      tree line_cst = build_int_cst(integer_type_node, line);

      /* Insert __rt_bb_start(seq, file, line) before the first statement. */
      gimple_stmt_iterator si = gsi_start_bb(bb);
      gsi_insert_before(&si,
        gimple_build_call(bb_start_decl, 3, id_cst, file_str, line_cst),
        GSI_SAME_STMT);

      /* Insert __rt_bb_end(seq) before the terminator (branch/return).
       * gsi_last_bb points to the last statement, which is always the
       * terminator — we insert just before it so the hook fires while
       * still inside the BB, immediately before control leaves. */
      gimple_stmt_iterator ei = gsi_last_bb(bb);
      gsi_insert_before(&ei,
        gimple_build_call(bb_end_decl, 1, id_cst),
        GSI_SAME_STMT);
    }

    ++seq;
  }
}

/* ── GCC pass registration boilerplate ───────────────────────────────────── */

/*
 * GCC requires every plugin that modifies the IR to register itself as a
 * named "pass" in the compiler pipeline. This block is mandatory boilerplate:
 *   - pass_data describes the pass (name, type, required IR properties).
 *   - rt_bb_pass is the pass class; execute() is called once per function.
 * The verbosity here is from the GCC pass API, not our logic.
 */
namespace {
  const pass_data pd = {
    GIMPLE_PASS,        /* we operate on GIMPLE IR (not RTL or the AST) */
    "rt_bb_instrument", /* pass name, visible in -fdump-tree-all output  */
    OPTGROUP_NONE, TV_NONE, PROP_gimple_any, 0, 0, 0, 0
  };
  struct rt_bb_pass : gimple_opt_pass {
    rt_bb_pass(gcc::context* c) : gimple_opt_pass(pd, c) {}
    unsigned int execute(function*) override {
      instrument_basic_blocks(cfun);  /* cfun = current function being compiled */
      return 0;
    }
  };
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

/*
 * plugin_init is called by GCC when the plugin .so is loaded.
 * We register three callbacks:
 *   PLUGIN_INFO              — plugin metadata (version, help text)
 *   PLUGIN_PASS_MANAGER_SETUP — insert our pass after the "cfg" pass
 *   PLUGIN_FINISH_UNIT       — close the JSON map file at end of compilation
 */
int plugin_init(struct plugin_name_args* args, struct plugin_gcc_version* ver) {
  if (!plugin_default_version_check(ver, &gcc_version)) return 1;
  parse_args(args);

  static plugin_info        pi  = { .version = "0.1", .help = "BB PMU cycle profiler" };
  static register_pass_info rpi = { new rt_bb_pass(g), "cfg", 1, PASS_POS_INSERT_AFTER };

  register_callback(args->base_name, PLUGIN_INFO,               nullptr,       &pi);
  register_callback(args->base_name, PLUGIN_PASS_MANAGER_SETUP, nullptr,       &rpi);
  register_callback(args->base_name, PLUGIN_FINISH_UNIT,        finish_bb_map, nullptr);
  return 0;
}
