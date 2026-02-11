#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "context.h"
#include "function.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "cfgloop.h"

#include <string.h>
#include <vector>
#include <string>
#include <fstream>

int plugin_is_GPL_compatible;

static tree record_fn_decl = nullptr;
static tree loop_start_decl = nullptr;
static tree loop_end_decl = nullptr;

static const char* target_file = nullptr;
static int target_line = -1;
static const char* loopmap_path = "./loop_map.json";

struct LoopInfo {
  std::string file;
  int start_line;
  int end_line;
  int after_line;
  struct loop* loop_ptr;
};

static void ensure_record_decl() {
  if (record_fn_decl) return;
  tree ret_type = void_type_node;
  tree param_types = tree_cons(NULL_TREE, build_pointer_type(char_type_node),
                     tree_cons(NULL_TREE, integer_type_node, NULL_TREE));
  tree fntype = build_function_type(ret_type, param_types);

  record_fn_decl = build_fn_decl("__rt_record_line", fntype);
  DECL_EXTERNAL(record_fn_decl) = 1;
  TREE_PUBLIC(record_fn_decl) = 1;
}

static void ensure_loop_decl() {
  if (loop_start_decl && loop_end_decl) return;
  tree ret_type = void_type_node;
  tree param_types = tree_cons(NULL_TREE, build_pointer_type(char_type_node),
                     tree_cons(NULL_TREE, integer_type_node, NULL_TREE));
  tree fntype = build_function_type(ret_type, param_types);

  loop_start_decl = build_fn_decl("__rt_loop_start", fntype);
  DECL_EXTERNAL(loop_start_decl) = 1;
  TREE_PUBLIC(loop_start_decl) = 1;

  loop_end_decl = build_fn_decl("__rt_loop_end", fntype);
  DECL_EXTERNAL(loop_end_decl) = 1;
  TREE_PUBLIC(loop_end_decl) = 1;
}

static bool has_path_sep(const char* s) {
  return s && strchr(s, '/');
}

static const char* basename_c(const char* s) {
  if (!s) return s;
  const char* p = strrchr(s, '/');
  return p ? p + 1 : s;
}

static bool match_file(const char* loc_file) {
  if (!target_file || !loc_file) return false;
  if (has_path_sep(target_file)) {
    return strcmp(loc_file, target_file) == 0;
  }
  return strcmp(basename_c(loc_file), target_file) == 0;
}

static bool match_target(const expanded_location& eloc) {
  if (!eloc.file || eloc.line == 0) return false;
  if (target_line < 0) return false;
  return match_file(eloc.file) && (eloc.line == target_line);
}

static void parse_args(struct plugin_name_args* info) {
  for (int i = 0; i < info->argc; ++i) {
    const char* key = info->argv[i].key;
    const char* val = info->argv[i].value;
    if (!key || !val) continue;
    if (strcmp(key, "target") == 0) {
      const char* colon = strrchr(val, ':');
      if (!colon) continue;
      static char file_buf[512];
      size_t len = (size_t)(colon - val);
      if (len >= sizeof(file_buf)) len = sizeof(file_buf) - 1;
      memcpy(file_buf, val, len);
      file_buf[len] = '\0';
      target_file = file_buf;
      target_line = atoi(colon + 1);
    } else if (strcmp(key, "loopmap") == 0) {
      loopmap_path = val;
    }
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

static int find_after_line(struct loop* loop) {
  auto_vec<edge> exits;
  get_loop_exit_edges(loop, &exits);
  edge e;
  unsigned ix;
  FOR_EACH_VEC_ELT(exits, ix, e) {
    expanded_location eloc = first_stmt_loc(e->dest);
    if (eloc.file && eloc.line > 0 && match_file(eloc.file)) {
      return eloc.line;
    }
  }
  return -1;
}

static std::vector<LoopInfo> collect_loops(function* fn) {
  std::vector<LoopInfo> out;
  if (!fn || !fn->cfg) return out;

  loop_optimizer_init(fn);
  loop_iterator li;
  struct loop* loop;

  FOR_EACH_LOOP(li, loop, 0) {
    expanded_location h = first_stmt_loc(loop->header);
    expanded_location l = first_stmt_loc(loop->latch ? loop->latch : loop->header);
    if (!h.file || h.line == 0) continue;

    LoopInfo info;
    info.file = h.file;
    info.start_line = h.line;
    info.end_line = (l.line > 0) ? l.line : h.line;
    info.after_line = find_after_line(loop);
    info.loop_ptr = loop;
    out.push_back(info);
  }

  return out;
}

static void write_loop_map(const std::vector<LoopInfo>& loops) {
  std::ofstream os(loopmap_path, std::ios::trunc);
  if (!os) return;
  os << "[\n";
  for (size_t i = 0; i < loops.size(); ++i) {
    const auto& L = loops[i];
    os << "  {\"file\":\"" << L.file
       << "\",\"start\":" << L.start_line
       << ",\"end\":" << L.end_line
       << ",\"after\":" << L.after_line << "}";
    if (i + 1 < loops.size()) os << ",";
    os << "\n";
  }
  os << "]\n";
}

static const LoopInfo* find_target_loop(const std::vector<LoopInfo>& loops) {
  const LoopInfo* best = nullptr;
  int best_span = 0x7fffffff;

  for (const auto& L : loops) {
    if (!match_file(L.file.c_str())) continue;
    if (target_line >= L.start_line && target_line <= L.end_line) {
      int span = L.end_line - L.start_line;
      if (span < best_span) {
        best_span = span;
        best = &L;
      }
    }
  }
  return best;
}

static void instrument_loop_boundary(const LoopInfo& L) {
  ensure_loop_decl();

  edge pe = loop_preheader_edge(L.loop_ptr);
  if (pe && pe->src) {
    tree file_str = build_string_literal(L.file.size() + 1, L.file.c_str());
    tree line_val = build_int_cst(integer_type_node, L.start_line);
    gimple* call = gimple_build_call(loop_start_decl, 2, file_str, line_val);
    gimple_stmt_iterator gsi = gsi_last_bb(pe->src);
    gsi_insert_after(&gsi, call, GSI_SAME_STMT);
  }

  auto_vec<edge> exits;
  get_loop_exit_edges(L.loop_ptr, &exits);
  edge e;
  unsigned ix;
  FOR_EACH_VEC_ELT(exits, ix, e) {
    basic_block bb = e->dest;
    if (!bb) continue;
    tree file_str = build_string_literal(L.file.size() + 1, L.file.c_str());
    tree line_val = build_int_cst(integer_type_node, L.end_line);
    gimple* call = gimple_build_call(loop_end_decl, 2, file_str, line_val);
    gimple_stmt_iterator gsi = gsi_start_bb(bb);
    gsi_insert_before(&gsi, call, GSI_SAME_STMT);
  }
}

static void instrument_function(function* fn) {
  if (!fn || !fn->cfg) return;
  if (!target_file || target_line < 0) return;
  ensure_record_decl();

  basic_block bb;
  FOR_ALL_BB_FN(bb, fn) {
    for (gimple_stmt_iterator gsi = gsi_start_bb(bb); !gsi_end_p(gsi); ) {
      gimple* stmt = gsi_stmt(gsi);

      location_t loc = gimple_location(stmt);
      if (loc == UNKNOWN_LOCATION) { gsi_next(&gsi); continue; }

      expanded_location eloc = expand_location(loc);
      if (!match_target(eloc)) { gsi_next(&gsi); continue; }

      tree file_str = build_string_literal(strlen(eloc.file) + 1, eloc.file);
      tree line_val = build_int_cst(integer_type_node, eloc.line);

      gimple* call = gimple_build_call(record_fn_decl, 2, file_str, line_val);
      gsi_insert_before(&gsi, call, GSI_SAME_STMT);
      return;
    }
  }
}

static unsigned int pass_exec() {
  auto loops = collect_loops(cfun);
  write_loop_map(loops);

  if (target_file && target_line >= 0) {
    if (const LoopInfo* L = find_target_loop(loops)) {
      instrument_loop_boundary(*L);
      loop_optimizer_finalize(cfun);
      return 0;
    }
  }

  instrument_function(cfun);
  loop_optimizer_finalize(cfun);
  return 0;
}

namespace {
  const pass_data pass_data_line = {
    GIMPLE_PASS, "rt_line_instrument", OPTGROUP_NONE, TV_NONE, PROP_gimple_any,
    0, 0, 0, 0, 0
  };

  struct pass_line_instrument : gimple_opt_pass {
    pass_line_instrument(gcc::context* ctxt) : gimple_opt_pass(pass_data_line, ctxt) {}
    unsigned int execute(function*) override { return pass_exec(); }
  };
}

static struct plugin_info plugin_info = {
  .version = "0.1",
  .help = "Per-line instrumentation with loop boundary fallback"
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
  return 0;
}
