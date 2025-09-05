const fs = require('node:fs');
const path = require('node:path');

const projectRoot = path.join(__dirname, '..');
const distDir = path.join(projectRoot, 'dist');

// Remove dist directory if it exists
if (fs.existsSync(distDir)) {
  fs.rmSync(distDir, { recursive: true, force: true });
  console.log('Removed existing dist directory');
}

// Create new dist directory
fs.mkdirSync(distDir, { recursive: true });
console.log('Created new dist directory');

// Files to copy
const filesToCopy = [
  'index.html',
  'src/main.js',
  'src/style.css'
];

// Copy each file to the dist directory
for (const file of filesToCopy) {
  const srcPath = path.join(projectRoot, file);
  // For files in src/, we want to copy them to the root of dist
  const destPath = file.startsWith('src/') 
    ? path.join(distDir, path.basename(file))
    : path.join(distDir, file);
  
  // Create directory structure if it doesn't exist
  const destDir = path.dirname(destPath);
  if (!fs.existsSync(destDir)) {
    fs.mkdirSync(destDir, { recursive: true });
  }
  
  if (fs.existsSync(srcPath)) {
    fs.copyFileSync(srcPath, destPath);
    console.log(`Copied ${file} to ${path.relative(projectRoot, destPath)}`);
  } else {
    console.warn(`Warning: ${file} not found in project`);
  }
}

console.log('File copy completed!');
