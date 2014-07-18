{
  "targets": [{
    "target_name": "core2dump",
    "type": "executable",
    "sources": [
      "src/common.c",
      "src/collector.c",
      "src/cli.c",
      "src/error.c",
      "src/strings.c",
      "src/v8constants.c",
      "src/v8helpers.c",
      "src/visitor.c",
    ],
    "conditions": [
      # Mach-O
      ["OS == 'mac'", {
        "sources": [
          "src/mac.c",
        ],
      }],
      # ELF
      ["OS == 'linux'", {
      }],
    ],
  }],
}
