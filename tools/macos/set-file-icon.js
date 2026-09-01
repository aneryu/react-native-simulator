ObjC.import("AppKit");

function run(argv) {
  if (argv.length < 2) {
    throw new Error("usage: set-file-icon.js <icon> <file>");
  }
  const image = $.NSImage.alloc.initWithContentsOfFile(argv[0]);
  if (image.isNil()) {
    throw new Error("failed to load icon: " + argv[0]);
  }
  const ok = $.NSWorkspace.sharedWorkspace.setIconForFileOptions(
      image, argv[1], 0);
  if (!ok) {
    throw new Error("failed to set icon on " + argv[1]);
  }
}
