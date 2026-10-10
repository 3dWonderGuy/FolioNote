/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "FolioNote", "index.html", [
    [ "Backup & Disaster Recovery Subsystem", "index.html", "index" ],
    [ "FolioNote I/O & User Data Persistence Workflow Specification", "md_src_2io_2_w_o_r_k_f_l_o_w.html", [
      [ "", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md423", null ],
      [ "Executive Summary & Purpose", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md424", null ],
      [ "Global Architecture & Pipeline Topology", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md426", null ],
      [ "Operational Workflows", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md428", [
        [ "3.1 Workflow 1: Crash-Resilient Page Persistence (.ink)", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md429", null ],
        [ "3.2 Workflow 2: Zero-Copy Asset Ingestion & Content-Addressable Storage", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md431", null ],
        [ "3.3 Workflow 3: Notebook Directory Package Branding (PackageMarker)", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md433", null ],
        [ "3.4 Workflow 4: Recursive Safe Directory Tree Cloning & Moves", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md435", null ]
      ] ],
      [ "Concurrency & Multi-Thread Safety Model", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md437", null ],
      [ "Threat Modeling & Failure Mode Recovery", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md439", [
        [ "5.1 Scenario 1: Power Loss / Kernel Panic During Save", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md440", null ],
        [ "5.2 Scenario 2: Antivirus or Cloud Storage (OneDrive / Dropbox) Lock Contention", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md441", null ],
        [ "5.3 Scenario 3: Destination File Marked FILE_ATTRIBUTE_READONLY", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md442", null ],
        [ "5.4 Scenario 4: Deep Directory Nesting Exceeding 260 Characters", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md443", null ]
      ] ],
      [ "Subsystem Verification & Test Matrix", "md_src_2io_2_w_o_r_k_f_l_o_w.html#autotoc_md445", null ]
    ] ],
    [ "FolioNote UI Subsystem Architecture & Workflow Reference", "md_src_2ui_2_w_o_r_k_f_l_o_w.html", [
      [ "", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md502", null ],
      [ "Table of Contents", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md503", null ],
      [ "System Vision & Standalone Framework Roadmap", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md505", [
        [ "The Extraction Objective", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md506", null ],
        [ "Core Design Rules Supporting Future Extraction:", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md507", null ]
      ] ],
      [ "High-Level Architecture & Layered Hierarchy", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md509", null ],
      [ "Complete UI Source Tree Outline", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md511", null ],
      [ "End-to-End Frame Orchestration & Presentation Lifecycle", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md513", null ],
      [ "The Framework Engine (src/ui/framework/)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md515", [
        [ "5.1 Design Tokens & Elevation Hierarchy (ui_tokens.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md516", [
          [ "Spacing Tokens:", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md517", null ],
          [ "Corner Radii:", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md518", null ],
          [ "Elevation Levels:", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md519", null ]
        ] ],
        [ "5.2 Dynamic Physics Animation Engine (ui_animation_manager.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md521", null ],
        [ "5.3 Surface Primitives & Glassmorphic Shapes (ui_shape_manager.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md523", null ],
        [ "5.4 Fluent Declarative Widget Builders (ui_builder.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md525", null ],
        [ "5.5 Animated Responsive Sidebar Rail (ui_sidebar.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md527", null ],
        [ "5.6 Global Overlay, Modal & Toast Host (ui_overlay_host.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md529", null ]
      ] ],
      [ "Component Layer & Shell Orchestration", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md531", [
        [ "6.1 Window Shell Geometry Calculator (app_shell.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md532", [
          [ "Ribbon Display Modes:", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md533", null ]
        ] ],
        [ "6.2 Custom Borderless TitleBar (custom_titlebar.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md535", null ],
        [ "6.3 Office 365-Style Ribbon Bar (ribbon_bar.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md536", null ],
        [ "6.4 Hierarchical Navigation Drawer (modern_nav_panel.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md537", null ]
      ] ],
      [ "Overlays, Diagnostics & Modals (src/ui/overlays/)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md539", [
        [ "7.1 Command Palette (command_palette.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md540", null ],
        [ "7.2 Real-Time Telemetry HUD (debug_overlay.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md541", null ],
        [ "7.3 Live Inking Physics Studio (tuning_overlay.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md542", null ],
        [ "7.4 External PDF Import Modal (pdf_import_modal.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md543", null ]
      ] ],
      [ "Dedicated Application Views (src/ui/views/)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md545", [
        [ "8.1 Notebook Hub (notebook_hub.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md546", null ],
        [ "8.2 Standalone Continuous PDF Viewer (pdf_viewer_page.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md547", null ]
      ] ],
      [ "Vector Icon Engine & Typography Pipeline", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md549", [
        [ "Vector Icon Cache (icon_manager.hpp)", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md550", null ],
        [ "Typography Engine", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md551", null ]
      ] ],
      [ "Mathematical Foundations & Visual Algorithms", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md553", [
        [ "10.1 Second-Order Damped Spring Oscillator Physics", "md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md554", null ]
      ] ]
    ] ],
    [ "FolioNote Diagnostic Error Codes", "md_src_2utils_2error__codes.html", [
      [ "Error Code Partitions", "md_src_2utils_2error__codes.html#autotoc_md557", null ],
      [ "System & OS Codes (1000 – 1999)", "md_src_2utils_2error__codes.html#autotoc_md559", [
        [ "ERR-1001: SysOutOfMemory", "md_src_2utils_2error__codes.html#autotoc_md560", null ],
        [ "ERR-1002: SysFileNotFound", "md_src_2utils_2error__codes.html#autotoc_md561", null ],
        [ "ERR-1004: SysFileAccessDenied", "md_src_2utils_2error__codes.html#autotoc_md562", null ]
      ] ],
      [ "Storage & Database Codes (2000 – 2999)", "md_src_2utils_2error__codes.html#autotoc_md564", [
        [ "ERR-2001: DbOpenFailed", "md_src_2utils_2error__codes.html#autotoc_md565", null ],
        [ "ERR-2002: DbSchemaMigrationFailed", "md_src_2utils_2error__codes.html#autotoc_md566", null ],
        [ "ERR-2004: DbDisconnected", "md_src_2utils_2error__codes.html#autotoc_md567", null ],
        [ "ERR-2101: RepoBlobWriteFailed", "md_src_2utils_2error__codes.html#autotoc_md568", null ]
      ] ],
      [ "Document Subsystem Codes (3000 – 3999)", "md_src_2utils_2error__codes.html#autotoc_md570", [
        [ "ERR-3001: DocWorkspaceLoadFailed", "md_src_2utils_2error__codes.html#autotoc_md571", null ],
        [ "ERR-3002: DocLibraryNotFound", "md_src_2utils_2error__codes.html#autotoc_md572", null ],
        [ "ERR-3010: DocNotebookNotFound", "md_src_2utils_2error__codes.html#autotoc_md573", null ],
        [ "ERR-3011: DocNotebookLoadFailed", "md_src_2utils_2error__codes.html#autotoc_md574", null ],
        [ "ERR-3020: DocSectionNotFound", "md_src_2utils_2error__codes.html#autotoc_md575", null ],
        [ "ERR-3040: DocPageNotFound", "md_src_2utils_2error__codes.html#autotoc_md576", null ],
        [ "ERR-3042: DocPageUnloaded", "md_src_2utils_2error__codes.html#autotoc_md577", null ]
      ] ],
      [ "Input Subsystem Codes (4000 – 4999)", "md_src_2utils_2error__codes.html#autotoc_md579", [
        [ "ERR-4001: InputWindowUnstable", "md_src_2utils_2error__codes.html#autotoc_md580", null ],
        [ "ERR-4002: InputTargetPageNull", "md_src_2utils_2error__codes.html#autotoc_md581", null ],
        [ "ERR-4010: InputGestureUnrecognized", "md_src_2utils_2error__codes.html#autotoc_md582", null ],
        [ "ERR-4040: InputActionTargetMissing", "md_src_2utils_2error__codes.html#autotoc_md583", null ]
      ] ]
    ] ],
    [ "FolioNote Architecture & System Workflow Reference", "md_src_2_w_o_r_k_f_l_o_w.html", [
      [ "", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md631", null ],
      [ "Table of Contents", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md632", null ],
      [ "System Architecture Overview", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md634", null ],
      [ "Complete Source Code Tree Outline", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md636", null ],
      [ "End-to-End Runtime Lifecycle", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md638", null ],
      [ "Multimodal Input Pipeline (Hardware to Canvas)", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md640", null ],
      [ "Input State Machine & Interaction States", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md642", [
        [ "5.1 State Transition Matrix", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md643", null ],
        [ "5.2 Interaction State Summary Table", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md644", null ]
      ] ],
      [ "Inking, Smoothing & Real-Time Render Pipeline", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md646", [
        [ "Inking Latency Optimizations:", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md647", null ]
      ] ],
      [ "Spatial Indexing & Hit-Testing Architecture", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md649", [
        [ "Hit-Test Acceleration Rules:", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md650", null ]
      ] ],
      [ "UI Orchestration & Frame Presentation", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md652", null ],
      [ "Storage, Database & Persistence System", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md654", [
        [ "9.1 Package Container Architecture", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md655", null ],
        [ "9.2 Relational Database Schema (structure.db)", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md656", null ],
        [ "9.3 Binary .ink Serialization Format", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md657", null ],
        [ "9.4 Asynchronous Save & Working Set LRU Pipeline", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md658", null ]
      ] ],
      [ "Command History & Transactional Undo/Redo", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md660", [
        [ "Transaction Boundaries:", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md661", null ]
      ] ],
      [ "Mathematical Foundations & Algorithms", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md663", [
        [ "11.1 Affine Coordinate Transformation Pipeline", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md664", [
          [ "Screen to World Projection:", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md665", null ],
          [ "World to Screen Projection:", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md666", null ]
        ] ],
        [ "11.2 Spring-Mass-Damper Stroke Smoothing Physics", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md668", null ],
        [ "11.3 Centripetal Catmull-Rom Spline Formulation", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md670", null ],
        [ "11.4 Selection & Eraser Ray-Segment Collision Math", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md672", null ]
      ] ],
      [ "Summary", "md_src_2_w_o_r_k_f_l_o_w.html#autotoc_md674", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"_your_source_here_8c.html",
"class_canvas_engine.html#a2f2b0cf608b3779c05ecf47fd0f28bd7",
"class_canvas_transform.html#ab06703d147e1451c3cd5dd3217a635d6",
"class_folio_1_1_app_directories.html#a4043a950b1e6de5675efb28bd5dbde61",
"class_folio_1_1_batch_erase_command.html#a75ec4f304c18d2b0f1fd186fabf1554e",
"class_folio_1_1_d_b_manager.html#a5924113982c29af24973eab0d9c07dc3",
"class_folio_1_1_file_reader.html#ae6a4ef56a017d39d569e1a24968ed3b8",
"class_folio_1_1_import_manager.html",
"class_folio_1_1_link_object.html#a3cac596f9c9e2e9836cb51b944612af0",
"class_folio_1_1_md_editor_state.html#a5a6bf255b521e672d3b3ff376b9695fc",
"class_folio_1_1_mock_dummy_overlay.html#af8e862dd54c89e12981dff69c41e1016",
"class_folio_1_1_pdf_container.html#a3bb43ff63abb136f874dc2fe7cb992e8",
"class_folio_1_1_pdf_tile_cache.html#a62412c9baf896acdfcc48df7bfbc8024",
"class_folio_1_1_physics_1_1_inertial_tracker1_d.html#a3dd78969eb0b054c62c6275c59ffa6ee",
"class_folio_1_1_shape_object.html#a4ca2612bba58a8959b823e7213a2d7e8",
"class_folio_1_1_text_editor_state.html#ac0423dacb696a382d50165c4ebd2684f",
"class_folio_1_1_u_i_1_1_u_i_sidebar_component.html#a7c674b4b16ac6125ed1aa4b567016eea",
"class_folio_1_1_wave_shape_generator.html",
"class_folio_u_i_1_1_toolbar_section_builder.html#a3d66d916727cc0b7d9c9f8bcab5d9fa5",
"class_preset_manager.html#aa4be7563e0930dc565b5116238afca15",
"class_settings_manager.html#afa168170d7bfa8512d37a5d1a6d54853",
"embedded__app__layer_8cpp.html",
"image__format_8hpp.html#a928807520f4d9870f1665a5420498729acd15a75c26008696647b31a3f0de43b3",
"md_src_2ui_2_w_o_r_k_f_l_o_w.html#autotoc_md518",
"namespace_folio.html#aa5d0a5c237fbb9fb9e0b83bb0275e13ca505a83f220c02df2f85c3810cd9ceb38",
"namespace_folio_input.html#a273a0fccf5d4175ca413cbd0c3836149a3d53ca4deee33f45422d3ba300b44cfc",
"pen__palette_8hpp.html#a78db2b3e38509743bd78678a43926255ab00adf4f51f5e2f5090416a006cf248c",
"struct_canvas_engine_1_1_eraser_visual_state.html#acb5ccb8872217b7d15ae65fef3e5dc99",
"struct_folio_1_1_app_screen_region.html#af6ff41da33bc0aa372e6e6beb818d5a5",
"struct_folio_1_1_d_b_section_record.html#a96ccbe6089f98e3a20d5fc0b54bebd49",
"struct_folio_1_1_md_span.html#a8bf2ed014ee656f59d21d8c984e2ddc8",
"struct_folio_1_1_pdf_doc_holder.html#a27b6dba50371165fc25fbf8ac8efe1af",
"struct_folio_1_1_search_index_entry.html#ad0e478895729d4b190ae4861b8ffbcd3",
"struct_folio_1_1_u_i_1_1_radii.html#a89e1c3084030caf955baab928ced200f",
"struct_folio_u_i_1_1_toolbar_item_def.html#ab2f8761c5c2782ec13a40f752a0ee8b6",
"struct_pen_tool.html#af67dd2a57d62d4ac925120279cf12be3",
"wave__shapes_8cpp_source.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';