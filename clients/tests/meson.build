test_protocols = []
protocol_dir = meson.project_source_root() / '../build/host/sources/wlroots-0.20.2/protocol'
foreach protocol : ['wlr-screencopy-unstable-v1', 'wlr-virtual-pointer-unstable-v1', 'virtual-keyboard-unstable-v1']
  xml = protocol_dir / (protocol + '.xml')
  test_protocols += custom_target(protocol + '-header', input: xml,
    output: protocol + '-client-protocol.h', command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  test_protocols += custom_target(protocol + '-code', input: xml,
    output: protocol + '-protocol.c', command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach
executable('client-exercise', ['exercise.c', test_protocols,
  '../../common/ps5wl-client/ps5wl-client.c', '../../common/ps5log/ps5log.c'],
  dependencies: [wayland, xkb, threads])
