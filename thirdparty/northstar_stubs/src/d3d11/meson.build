# forked from dxvk @ ecd7b67069a6e2a4b78a8a929fd287030d283ccb (2021-12-17)

d3d11_res = [
  windows.compile_resources('version.rc'),
]

util_src = [
  'com_private_data.cc',
]

dxgi_src = [
  'dxgi_adapter.cc',
  'dxgi_factory.cc',
  'dxgi_output.cc',
]

d3d11_src = [
  'd3d11_annotation.cc',
  'd3d11_blend.cc',
  'd3d11_buffer.cc',
  'd3d11_class_linkage.cc',
  'd3d11_cmdlist.cc',
  'd3d11_context.cc',
  'd3d11_context_def.cc',
  'd3d11_context_imm.cc',
  'd3d11_depth_stencil.cc',
  'd3d11_device.cc',
  'd3d11_input_layout.cc',
  'd3d11_main.cc',
  'd3d11_query.cc',
  'd3d11_rasterizer.cc',
  'd3d11_resource.cc',
  'd3d11_sampler.cc',
  'd3d11_state.cc',
  'd3d11_state_object.cc',
  'd3d11_swapchain.cc',
  'd3d11_texture.cc',
  'd3d11_view_dsv.cc',
  'd3d11_view_rtv.cc',
  'd3d11_view_srv.cc',
  'd3d11_view_uav.cc',
]

shared_library('d3d11', util_src + dxgi_src + d3d11_src + d3d11_res,
  name_prefix    : '',
  install        : true,
  vs_module_defs : 'd3d11.def',
)
