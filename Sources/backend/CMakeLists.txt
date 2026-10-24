target_sources(kura-runtime
  PUBLIC compile_type.cc
         effect_visitor.cc
         flow_graph.cc
         flow_graph_builder.cc
         ir.cc
         ir_printer.cc
         jit.cc
         llvm_codegen.cc
         runtime_abi.cc
         type_analysis.cc
         value_visitor.cc)
