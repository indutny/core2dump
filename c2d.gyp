{
  "targets": [{
    "target_name": "core2dump",
    "type": "executable",
    "include_dirs": [ "src" ],
    "sources": [
      "src/common.c",
      "src/collector.c",
      "src/cli.c",
      "src/error.c",
      "src/obj.c",
      "src/obj/dwarf.c",
      "src/strings.c",
      "src/v8constants.c",
      "src/v8helpers.c",
      "src/visitor.c",
    ],
    "conditions": [
      # Mach-O
      ["OS == 'mac'", {
        "sources": [
          "src/obj/mach.c",
        ],
      }],
      # ELF
      ["OS == 'linux' or OS == 'freebsd'", {
        "sources": [
          "src/obj/elf.c",
        ],
      }],
    ],
  },
    {
      "target_name": "copy_binary",
      "type":"none",
      "dependencies" : [ "core2dump" ],
      "copies":
        [
        {
          'destination': '<(module_root_dir)/bin/',
          'files': ['<(module_root_dir)/build/Release/core2dump']
        }
        ]
    }
  ],
}
