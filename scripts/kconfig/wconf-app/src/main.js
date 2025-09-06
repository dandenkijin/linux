// Check if we're running in a Tauri environment
const isTauri = window.__TAURI__ !== undefined;

// Tauri imports - will be tree-shaken in web builds
let invoke = null;
let listen = null;
let appWindow = null;

if (isTauri) {
  import('@tauri-apps/api/tauri').then(module => { invoke = module.invoke; });
  import('@tauri-apps/api/event').then(module => { listen = module.listen; });
  import('@tauri-apps/api/window').then(module => { appWindow = module.appWindow; });
}

// --- State Management ---
const state = {
    kconfigTree: [],
    selectedOption: null,
    setSelectedOption: function(option) {
        this.selectedOption = option;
    }
};

// Initialize the application
async function initializeApp() {
  console.log('[INIT] Initializing application...');
  
  if (!isTauri) {
    console.log('[INIT] Running in web environment');
    showWebWarning();
    return;
  }
  
  try {
    await initializeTauri();
  } catch (error) {
    handleInitializationError(error);
  }
}

async function initializeTauri() {
  console.log('[INIT] Running in Tauri environment');
  
  // Wait for the Tauri API to be ready
  console.log('[INIT] Waiting for Tauri window to be ready...');
  await appWindow.show();
  console.log('[INIT] Tauri app window is ready');
  
  // Set up window event listeners
  setupWindowListeners();
  
  // Log Tauri version info
  const { version, tauriVersion } = await import('@tauri-apps/api/app');
  console.log(`[INIT] App version: ${version}, Tauri version: ${tauriVersion}`);
}

function setupWindowListeners() {
  appWindow.onCloseRequested(() => {
    console.log('[WINDOW] Close requested');
  });
}

function showWebWarning() {
  const appRoot = document.getElementById('app');
  if (!appRoot) return;
  
  appRoot.innerHTML += `
    <div class="warning">
      <p>Running in web mode with limited functionality. For full features, please use the Tauri app.</p>
    </div>`;
}

function handleInitializationError(error) {
  console.error('Error initializing Tauri:', error);
  
  const appRoot = document.getElementById('app');
  if (!appRoot) return;
  
  appRoot.innerHTML = `
    <div class="error">
      <h2>Initialization Error</h2>
      <p>${error.message || 'Failed to initialize application'}</p>
      <p>Check the console for more details.</p>
    </div>`;
}

// --- Main App Setup ---
function setupUI() {
  const appRoot = document.getElementById("app");
  if (!appRoot) {
    console.error("App root element #app not found!");
    return;
  }

  // Create the basic UI structure
  appRoot.innerHTML = `
        <div class="app-container">
            <header>
                <h1>Kernel Configuration</h1>
                <div class="controls">
                    <select
                        id="kconfig-type-filter"
                        title="Filter by configuration type"
                    >
                        <option value="all">All Types</option>
                        <option value="bool">Boolean</option>
                        <option value="tristate">Tristate</option>
                        <option value="string">String</option>
                        <option value="int">Integer</option>
                        <option value="hex">Hex</option>
                    </select>
                    <button id="saveBtn">Save .config</button>
                </div>
            </header>
            <main>
                <div class="sidebar">
                    <input type="text" id="searchBox" placeholder="Search options...">
                    <div id="configTree"><p>Loading Kconfig tree...</p></div>
                </div>
                <div class="content">
                    <div id="configDetails"><p>Select a configuration option to see its details.</p></div>
                </div>
            </main>
        </div>
    `;

  // Get references to newly created elements
  const treeContainer = document.getElementById("configTree");
  const detailsContainer = document.getElementById("configDetails");
  const searchBox = document.getElementById("searchBox");
  const saveBtn = document.getElementById("saveBtn");

  const typeFilterDropdown = document.getElementById("kconfig-type-filter");

  // This function contains all logic that depends on the Tauri backend.
  const setupApp = () => {
    console.log("[FRONTEND] setupApp called, loading data.");
    loadKconfigData(treeContainer, detailsContainer);
    searchBox.addEventListener("input", applyFilters);
    typeFilterDropdown.addEventListener("change", applyFilters);
    saveBtn.addEventListener("click", saveConfig);
  };

  // The "command not found" error is a race condition where the frontend tries
  // to call `invoke` before the Rust backend has initialized the webview.
  // We listen for a backend event to know when it's safe to proceed.
  // 'tauri://window-created' is a reliable event that fires when the webview is ready.
  listen("tauri://window-created", setupApp).catch(() => {
    console.warn(
      "Could not listen for 'tauri://window-created', falling back to a timeout.",
    );
    // If for some reason the event fails, a timeout is a robust fallback.
    setTimeout(setupApp, 250);
  });
}

// --- Event Handling & Filtering ---
function applyFilters() {
  const treeContainer = document.getElementById("configTree");
  const searchTerm = document.getElementById("searchBox").value.toLowerCase();
  const typeFilter = document.getElementById("kconfig-type-filter").value;

  const filteredTree = filterTree(kconfigTree, searchTerm, typeFilter);
  renderTree(filteredTree, treeContainer);
}

// --- Data Loading ---
async function loadKconfigData(treeContainer, detailsContainer) {
  try {
    console.log("[FRONTEND] Starting to load Kconfig data...");
    console.log("[FRONTEND] Calling invoke('load_kconfig')...");
    
    // Add loading indicator
    treeContainer.innerHTML = '<div class="loading">Loading configuration, please wait...</div>';
    
    // Call the backend to load the Kconfig
    const startTime = performance.now();
    const kconfigTree = await invoke("load_kconfig");
    const loadTime = (performance.now() - startTime).toFixed(2);
    
    console.log("[FRONTEND] Backend returned successfully");
    console.log(`[FRONTEND] Received tree with ${kconfigTree ? kconfigTree.length : 0} top-level nodes`);
    console.log(`[FRONTEND] Loaded in ${loadTime}ms`);
    
    if (!kconfigTree || kconfigTree.length === 0) {
      console.warn("[FRONTEND] Received empty Kconfig tree");
      treeContainer.innerHTML = '<div class="error">No configuration options found. The Kconfig file may be empty or invalid.</div>';
      return;
    }
    
    // Render the tree
    console.log("[FRONTEND] Rendering configuration tree...");
    renderTree(kconfigTree, treeContainer);
    
    // Show default message
    detailsContainer.innerHTML = "<p>Select a configuration option to see its details.</p>";
    
    console.log("[FRONTEND] Kconfig data loaded and rendered successfully");
  } catch (error) {
    console.error("Failed to load Kconfig tree:", error);
    const errorMessage = error.message || String(error);
    console.error("Error details:", error);
    
    // Show detailed error to user
    treeContainer.innerHTML = `
      <div class="error">
        <h2>Error Loading Configuration</h2>
        <p><strong>Error:</strong> ${errorMessage}</p>
        <p>Please check the console for more details.</p>
      </div>
    `;
  }
}

// --- Helper Functions ---
function renderConfigControl(config) {
  const { type, value } = config;
  
  switch (type) {
    case 'bool':
    case 'tristate': {
      const states = type === 'bool' ? ['n', 'y'] : ['n', 'm', 'y'];
      let currentIndex = states.indexOf(value);
      if (currentIndex === -1) currentIndex = 0;
      
      return `
        <div class="toggle-control">
          <button class="toggle-btn" data-next-value="${states[(currentIndex + 1) % states.length]}">
            ${getValueDisplay(value, type)}
          </button>
        </div>
      `;
    }
    
    case 'string': {
      return `
        <input type="text" class="string-input" value="${value || ''}" 
               data-original-value="${value || ''}" />
      `;
    }
    
    case 'int':
    case 'hex': {
      return `
        <input type="${type === 'hex' ? 'text' : 'number'}" 
               class="${type}-input" 
               value="${value || '0'}" 
               data-original-value="${value || '0'}" 
               ${type === 'hex' ? 'pattern="0x?[0-9a-fA-F]+"' : 'min="0"'}
        />
      `;
    }
    
    default: {
      return `<span class="value-display">${value || ''}</span>`;
    }
  }
}

function getValueDisplay(value, type) {
  if (type === 'bool') {
    return value === 'y' ? 'Yes' : 'No';
  }
  if (type === 'tristate') {
    return { 'y': 'Yes', 'm': 'Module', 'n': 'No' }[value] || value;
  }
  return value;
}

function showConfigDetails(config, container) {
  if (!container) return;
  
  const { name, prompt, type, help, default: defaultValue, depends, select } = config;
  
  container.innerHTML = `
    <div class="config-details">
      <h2>${prompt || name}</h2>
      ${name ? `<div class="detail-row"><strong>Name:</strong> <code>${name}</code></div>` : ''}
      ${type ? `<div class="detail-row"><strong>Type:</strong> ${type}</div>` : ''}
      
      ${defaultValue ? `
        <div class="detail-row">
          <strong>Default:</strong> <code>${defaultValue}</code>
        </div>
      ` : ''}
      
      ${depends ? `
        <div class="detail-row">
          <strong>Depends on:</strong> <code>${depends}</code>
        </div>
      ` : ''}
      
      ${select ? `
        <div class="detail-row">
          <strong>Selects:</strong> <code>${select}</code>
        </div>
      ` : ''}
      
      ${help ? `
        <div class="help-text">
          <h3>Help Text</h3>
          <p>${help.replace(/\n/g, '<br>')}</p>
        </div>
      ` : ''}
      
      <div class="actions">
        <button class="btn btn-apply">Apply Changes</button>
        <button class="btn btn-cancel">Cancel</button>
      </div>
    </div>
  `;
  
  // Add event listeners for action buttons
  container.querySelector('.btn-apply')?.addEventListener('click', () => {
    saveConfigValue(name, getCurrentValue(config, container));
  });
  
  container.querySelector('.btn-cancel')?.addEventListener('click', () => {
    // Reset to original value
    const input = container.querySelector('input, select, button[data-original-value]');
    if (input) {
      input.value = input.dataset.originalValue || '';
    }
  });
}

function getCurrentValue(config, container) {
  const input = container?.querySelector('input, select, button[data-next-value]');
  
  if (!input) return config.value;
  
  if (input.dataset.nextValue) {
    return input.dataset.nextValue;
  }
  
  return input.value;
}

async function saveConfigValue(name, value) {
  try {
    const success = await invoke('set_kconfig_option', { name, value });
    if (success) {
      // Update UI to reflect the new value
      const item = document.querySelector(`[data-name="${name}"]`);
      if (item) {
        const valueDisplay = item.querySelector('.value-display, .toggle-btn');
        if (valueDisplay) {
          valueDisplay.textContent = getValueDisplay(value, item.dataset.type);
          valueDisplay.dataset.nextValue = value;
        }
      }
      showNotification('Configuration saved successfully', 'success');
    } else {
      showNotification('Failed to save configuration', 'error');
    }
  } catch (error) {
    console.error('Error saving configuration:', error);
    showNotification(`Error: ${error.message || 'Unknown error'}`, 'error');
  }
}

function showNotification(message, type = 'info') {
  const notification = document.createElement('div');
  notification.className = `notification ${type}`;
  notification.textContent = message;
  
  document.body.appendChild(notification);
  
  setTimeout(() => {
    notification.classList.add('show');
    setTimeout(() => {
      notification.classList.remove('show');
      setTimeout(() => notification.remove(), 300);
    }, 3000);
  }, 100);
}

// --- Tree Rendering Logic ---
function renderTree(nodes, parentElement) {
  parentElement.innerHTML = "";
  if (!nodes || nodes.length === 0) {
    parentElement.innerHTML =
      '<p class="empty-tree">No items match your search.</p>';
    return;
  }

  const ul = document.createElement("ul");
  ul.className = "tree-view";

  for (const node of nodes) {
    const li = createNodeElement(node);
    if (li) ul.appendChild(li);
  }

  parentElement.appendChild(ul);
}

function createNodeElement(node) {
  const li = document.createElement("li");
  li.className = "tree-item";

  const nodeType = Object.keys(node)[0];
  const data = node[nodeType];
  const children = data.children || [];
  const isExpandable = children.length > 0;
  const isExpanded = false;

  let content = "";
  switch (nodeType) {
    case "Config":
      li.dataset.name = data.name;
      li.dataset.type = data.type || "unknown";
      
      // Create a more detailed config item
      content = `
        <div class="config-item">
          <span class="config-prompt">${data.prompt || data.name}</span>
          <div class="config-controls">
            ${renderConfigControl(data)}
          </div>
        </div>
      `;
      
      // Add click handler to show details
      li.addEventListener('click', (e) => {
        if (!e.target.closest('.config-controls')) {
          showConfigDetails(data, document.getElementById('configDetails'));
        }
        e.stopPropagation();
      });
      break;
      
    case "Menu":
      content = `
        <div class="menu-item">
          <span class="menu-icon">📁</span>
          <span class="menu-title">${data.prompt || 'Menu'}</span>
        </div>
      `;
      break;
      
    case "Choice":
      content = `
        <div class="choice-item">
          <span class="choice-icon">☑️</span>
          <span class="choice-title">${data.prompt || 'Choice'}</span>
        </div>
      `;
      break;
      
    case "Comment":
      content = `<div class="comment-item"># ${data}</div>`;
      break;
      
    default:
      return null;
  }

  const nodeDiv = document.createElement("div");
  nodeDiv.className = `tree-node ${nodeType.toLowerCase()}-node`;
  nodeDiv.innerHTML = `
    <div class="node-header">
      ${(() => {
        if (!isExpandable) return '<span class="toggle"> </span>';
        const icon = isExpanded ? '▼' : '▶';
        return `<span class="toggle">${icon}</span>`;
      })()}
      ${content}
    </div>
  `;
  
  // Add expand/collapse toggle
  if (isExpandable) {
    const toggle = nodeDiv.querySelector('.toggle');
    const childrenUl = document.createElement("ul");
    childrenUl.className = `children ${isExpanded ? '' : 'collapsed'}`;
    
    toggle.addEventListener('click', (e) => {
      e.stopPropagation();
      const wasExpanded = !childrenUl.classList.contains('collapsed');
      childrenUl.classList.toggle('collapsed', wasExpanded);
      toggle.textContent = wasExpanded ? '▶' : '▼';
    });
    
    // Render children
    renderTree(children, childrenUl);
    li.appendChild(childrenUl);
  }
  
  li.appendChild(nodeDiv);
  
  // Add click handler for config items
  if (nodeType === "Config") {
    nodeDiv.addEventListener("click", (e) => {
      if (!e.target.closest('.config-controls')) {
        showConfigDetails(data, document.getElementById('configDetails'));
      }
      e.stopPropagation();
    });
  }
  
  return li;
}

// --- Details Pane Logic ---
function getValueControls(option) {
  const { type, name, value } = option;
  const checked = (val) => value === val ? 'checked' : '';
  
  switch (type) {
    case "bool":
      return `
        <label><input type="radio" name="${name}" value="y" ${checked('y')}> Yes</label>
        <label><input type="radio" name="${name}" value="n" ${checked('n')}> No</label>
      `;
    case "tristate":
      return `
        <label><input type="radio" name="${name}" value="y" ${checked('y')}> Yes</label>
        <label><input type="radio" name="${name}" value="m" ${checked('m')}> Module</label>
        <label><input type="radio" name="${name}" value="n" ${checked('n')}> No</label>
      `;
    case "string":
      return `<input type="text" id="stringValue" value="${value.replace(/"/g, '&quot;')}">`;
    case "int":
    case "hex":
      return `<input type="text" id="numericalValue" value="${value}">`;
    default:
      return '';
  }
}

function setupEventListeners(container) {
  for (const input of container.querySelectorAll("input")) {
    const eventType = ["radio", "checkbox"].includes(input.type) ? "change" : "input";
    input.addEventListener(eventType, handleValueChange);
  }
}

function renderOptionDetails(option) {
  return `
    <h2>${option.name}</h2>
    <p><strong>Prompt:</strong> ${option.prompt}</p>
    <p><strong>Type:</strong> ${option.type}</p>
    <div class="value-controls">
      <strong>Value:</strong> ${getValueControls(option)}
    </div>
    ${option.depends ? `<p><strong>Depends on:</strong> <code>${option.depends}</code></p>` : ""}
    <div class="help-text">
      <h3>Help</h3>
      <pre>${option.help || "No help text available."}</pre>
    </div>
  `;
}


// --- Backend Communication ---
async function handleValueChange(e) {
  if (!state.selectedOption) return;
  const { name } = state.selectedOption;
  const value = e.target.value;

  try {
    const success = await invoke("set_kconfig_option", { name, value });
    if (success) {
      const treeNodeValue = document.querySelector(
        `li[data-name="${name}"] .config-value`,
      );
      if (treeNodeValue) treeNodeValue.textContent = `(${value})`;
      state.setSelectedOption({ ...state.selectedOption, value });
    } else {
      console.warn(`Backend reported failure setting ${name}.`);
    }
  } catch (error) {
    console.error(`Error setting option ${name}:`, error);
    alert(`Error setting option: ${error.message || error}`);
  }
}

async function saveConfig() {
  try {
    await invoke("save_kconfig");
    alert("Configuration saved successfully!");
  } catch (error) {
    console.error("Failed to save config:", error);
    alert(`Error saving configuration: ${error.message || error}`);
  }
}

// --- Utility Functions ---
function filterTree(nodes, searchTerm, typeFilter) {
  // This is a recursive function that filters the tree based on search and type criteria.
  // A node is kept if it matches the criteria OR if any of its children match.
  return nodes
    .map((node) => {
      // Return a deep copy to avoid modifying the global state
      const clonedNode = JSON.parse(JSON.stringify(node));
      const nodeType = Object.keys(clonedNode)[0];
      const data = clonedNode[nodeType];

      // If the node has children, recursively filter them first.
      if (data.children && data.children.length > 0) {
        data.children = filterTree(data.children, searchTerm, typeFilter);
      }

      return clonedNode;
    })
    .filter((node) => {
      const nodeType = Object.keys(node)[0];
      const data = node[nodeType];

      // Condition 1: Keep the node if it has any children left after they were filtered.
      if (data.children && data.children.length > 0) {
        return true;
      }

      // Condition 2: If it's a leaf node (or has no matching children), check if it matches the filters itself.
      const searchMatch =
        !searchTerm ||
        data.prompt?.toLowerCase().includes(searchTerm) ||
        data.name?.toLowerCase().includes(searchTerm);

      // For non-Config nodes, we don't check type.
      if (nodeType !== "Config") {
        // A Menu/Choice etc. should only be kept if it matches the search term
        // AND we are not filtering by a specific type (since they don't have one).
        return searchMatch && typeFilter === "all";
      }

      // For Config nodes, check both search and type.
      const typeMatch = typeFilter === "all" || data.type === typeFilter;
      return searchMatch && typeMatch;
    });
}

// --- App Entry Point ---
document.addEventListener("DOMContentLoaded", () => {
  initializeApp().then(() => {
    setupUI();
  }).catch(error => {
    console.error('Failed to initialize app:', error);
  });
})

