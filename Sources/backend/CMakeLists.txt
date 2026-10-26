target_sources(kura-runtime
  PUBLIC c_compile.cc
         c_header.cc
         compile_type.cc
         effect_visitor.cc
         flow_graph.cc
         flow_graph_builder.cc
         ir.cc
         ir_printer.cc
         jit.cc
         llvm_codegen.cc
         purity_analysis.cc
         runtime_abi.cc
         type_analysis.cc
         value_visitor.cc)
