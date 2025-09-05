const fs = require('node:fs');
const path = require('node:path');

// Ensure dist directory exists
const distDir = path.join(__dirname, '..', 'dist');
if (!fs.existsSync(distDir)) {
  fs.mkdirSync(distDir, { recursive: true });
}

// Copy necessary files to dist
const filesToCopy = [
  'index.html',
  'main.js',
  'style.css',
  // Add any other files that need to be in dist
];

for (const file of filesToCopy) {
  const srcPath = path.join(__dirname, '..', file);
  const destPath = path.join(distDir, file);
  
  if (fs.existsSync(srcPath)) {
    fs.copyFileSync(srcPath, destPath);
    console.log(`Copied ${file} to dist/`);
  } else {
    console.warn(`Warning: ${file} not found in project root`);
  }
}

console.log('Build completed!');
