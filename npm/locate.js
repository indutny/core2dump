// Locate result of the build

var fs = require('fs');
var path = require('path');

var paths = [
  ['build'],
  ['build', 'Release'],
  ['build', 'Debug'],
  ['out', 'Release'],
  ['out', 'Debug'],
  ['build', 'default']
];

var exenames = [ 'core2dump', 'core2dump.exe' ];

var root = path.resolve(__dirname, '..');

for (var i = 0; i < paths.length; i++) {
  for (var j = 0; j < exenames.length; j++) {
    var exename = exenames[j];
    var filename = path.resolve.apply(path, [root].concat(paths[i], exename));

    try {
      fs.statSync(filename);
    } catch(e) {
      continue;
    }

    var out = path.resolve(root, 'npm', exename);
    console.error('Found binary %s', filename);
    try {
      fs.unlinkSync(out);
    } catch (e) {
    }
    console.error('Linking binary %s to %s', filename, out);
    try {
      fs.symlinkSync(filename, out);
    } catch (e) {
      // Ignore errors
    }
    process.exit(0);
  }
}

console.error('Build result not found');
process.exit(1);
