#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "context.h"
#include "function.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-pass.h"

#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <stdint.h>

int plugin_is_GPL_compatible;

static tree bb_start_decl = nullptr;
static tree bb_end_decl   = nullptr;

static const char* bbmap_path   = "./bb_map.json";
static int         target_bbid  = -1;
static const char* target_func  = "main";  /* only map/instrument this function */

struct BBMapEntry {
  int id;
  int seq;      /* sequential order within function, used to build id */
  int preds;
  int succs;
  std::string function;
  std::string file;
  int line;
};

static std::vector<BBMapEntry> bb_entries;

static expanded_location first_stmt_loc(basic_block bb);

static uint32_t fnv1a_32(const char* s) {
  uint32_t h = 2166136261u;
  if (!s) return h;
  for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
    h ^= (uint32_t)(*p);
    h *= 16777619u;
  }
  return h;
}

/* BB identity is a sequential counter per function (0, 1, 2, ...) assigned in
 * FOR_ALL_BB_FN traversal order, hashed with the function name.
 * This is independent of GCC's internal bb->index which can vary between
 * pass stages and compiler invocations. */
static int make_bb_id(const char* function_name, int seq) {
  uint32_t fhash = fnv1a_32(function_name);
  uint32_t id = ((fhash & 0x7fffU) << 16) | ((uint32_t)seq & 0xffffU);
  return (int)id;
}

static void ensure_bb_decl() {
  if (bb_start_decl && bb_end_decl) return;

  tree start_params = tree_cons(NULL_TREE, integer_type_node,
                      tree_cons(NULL_TREE, build_pointer_type(char_type_node),
                      tree_cons(NULL_TREE, integer_type_node, NULL_TREE)));
  tree start_type = build_function_type(void_type_node, start_params);
  bb_start_decl = build_fn_decl("__rt_bb_start", start_type);
  DECL_EXTERNAL(bb_start_decl) = 1;
  TREE_PUBLIC(bb_start_decl) = 1;

  tree end_params = tree_cons(NULL_TREE, integer_type_node, NULL_TREE);
  tree end_type = build_function_type(void_type_node, end_params);
  bb_end_decl = build_fn_decl("__rt_bb_end", end_type);
  DECL_EXTERNAL(bb_end_decl) = 1;
  TREE_PUBLIC(bb_end_decl) = 1;
}

static void parse_args(struct plugin_name_args* info) {
  for (int i = 0; i < info->argc; ++i) {
    const char* key = info->argv[i].key;
    const char* val = info->argv[i].value;
    if (!key || !val) continue;
    if (strcmp(key, "bbmap") == 0) {
      bbmap_path = val;
    } else if (strcmp(key, "bbid") == 0) {
      target_bbid = atoi(val);
    } else if (strcmp(key, "func") == 0) {
      target_func = val;
    }
  }
}

static void write_bb_map(void*, void*) {
  std::ofstream os(bbmap_path, std::ios::trunc);
  if (!os) return;
  os << "[\n";
  for (size_t i = 0; i < bb_entries.size(); ++i) {
    const auto& e = bb_entries[i];
    os << "  {\"id\":" << e.id
       << ",\"seq\":" << e.seq
       << ",\"preds\":" << e.preds
       << ",\"succs\":" << e.succs
       << ",\"function\":\"" << e.function
       << "\",\"file\":\"" << e.file
       << "\",\"line\":" << e.line << "}";
    if (i + 1 < bb_entries.size()) os << ",";
    os << "\n";
  }
  os << "]\n";
}

static void instrument_basic_blocks(function* fn) {
  if (!fn || !fn->cfg) return;

  /* Skip functions that are not the target. */
  if (!fn->decl || !DECL_NAME(fn->decl)) return;
  const char* function_name = IDENTIFIER_POINTER(DECL_NAME(fn->decl));
  if (!function_name || strcmp(function_name, target_func) != 0) return;

  ensure_bb_decl();

  basic_block bb;
  int seq = 0;  /* per-function sequential counter, stable across compilations */
  FOR_ALL_BB_FN(bb, fn) {
    if (!bb) continue;
    if (bb->index < NUM_FIXED_BLOCKS) continue;

    expanded_location eloc = first_stmt_loc(bb);
    const char* file = eloc.file ? eloc.file : "unknown";
    int line = eloc.line > 0 ? eloc.line : 0;

    int bb_id = make_bb_id(function_name, seq);
    int pred_count = (int)EDGE_COUNT(bb->preds);
    int succ_count = (int)EDGE_COUNT(bb->succs);
    bb_entries.push_back(BBMapEntry{bb_id, seq, pred_count, succ_count,
                    function_name, file, line});
    ++seq;

    /* BB mode is strictly per-BB: insert hooks only when bbid is explicitly selected. */
    if (target_bbid <= 0 || bb_id != target_bbid) {
      continue;
    }

    gimple_stmt_iterator start_it = gsi_start_bb(bb);
    if (gsi_end_p(start_it)) continue;

    tree id_val = build_int_cst(integer_type_node, bb_id);
    tree file_str = build_string_literal(strlen(file) + 1, file);
    tree line_val = build_int_cst(integer_type_node, line);

    gimple* start_call = gimple_build_call(bb_start_decl, 3, id_val, file_str, line_val);
    gsi_insert_before(&start_it, start_call, GSI_SAME_STMT);

    gimple_stmt_iterator end_it = gsi_last_bb(bb);
    gimple* end_call = gimple_build_call(bb_end_decl, 1, id_val);
    gsi_insert_before(&end_it, end_call, GSI_SAME_STMT);
  }
}

static expanded_location first_stmt_loc(basic_block bb) {
  expanded_location eloc = {};
  if (!bb) return eloc;
  gimple_stmt_iterator gsi = gsi_start_bb(bb);
  if (gsi_end_p(gsi)) return eloc;
  gimple* stmt = gsi_stmt(gsi);
  if (!stmt) return eloc;
  return expand_location(gimple_location(stmt));
}

static unsigned int pass_exec() {
  instrument_basic_blocks(cfun);
  return 0;
}

namespace {
  const pass_data pass_data_line = {
    GIMPLE_PASS, "rt_bb_instrument", OPTGROUP_NONE, TV_NONE, PROP_gimple_any,
    0, 0, 0, 0
  };

  struct pass_line_instrument : gimple_opt_pass {
    pass_line_instrument(gcc::context* ctxt) : gimple_opt_pass(pass_data_line, ctxt) {}
    unsigned int execute(function*) override { return pass_exec(); }
  };
}

static struct plugin_info plugin_info = {
  .version = "0.1",
  .help = "Basic-block and full-program instrumentation"
};

int plugin_init(struct plugin_name_args* plugin_info_args,
                struct plugin_gcc_version* version) {
  if (!plugin_default_version_check(version, &gcc_version)) {
    error("Incompatible GCC version");
    return 1;
  }

  parse_args(plugin_info_args);
  register_callback(plugin_info_args->base_name, PLUGIN_INFO, nullptr, &plugin_info);

  struct register_pass_info pass_info;
  pass_info.pass = new pass_line_instrument(g);
  pass_info.reference_pass_name = "cfg";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(plugin_info_args->base_name, PLUGIN_PASS_MANAGER_SETUP,
                    nullptr, &pass_info);
  register_callback(plugin_info_args->base_name, PLUGIN_FINISH_UNIT,
                    write_bb_map, nullptr);
  return 0;
}
