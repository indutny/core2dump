{
  "targets": [{
    "target_name": "core2dump",
    "type": "executable",
    "sources": [
      "src/core.c",
      "src/error.c",
      "src/common.c",
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
