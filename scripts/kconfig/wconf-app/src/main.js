import { invoke } from "@tauri-apps/api/tauri";
import { listen } from "@tauri-apps/api/event";

// --- Global State ---
let kconfigTree = [];
let selectedOption = null;

// --- Main App Setup ---
function initializeApp() {
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
    console.log("[FRONTEND] Calling invoke('load_kconfig')...");
    // The backend now knows the Kconfig path from its own command-line arguments.
    // We just need to tell it to start loading.
    console.log("[FRONTEND] Calling invoke('load_kconfig')...");
    kconfigTree = await invoke("load_kconfig");
    console.log(
      "[FRONTEND] invoke('load_kconfig') successful, received tree with length:",
      kconfigTree.length,
    );
    console.log("[FRONTEND] invoke('load_kconfig') successful.");
    renderTree(kconfigTree, treeContainer);
    detailsContainer.innerHTML =
      "<p>Select a configuration option to see its details.</p>";
  } catch (error) {
    console.error("Failed to load Kconfig tree:", error);
    detailsContainer.innerHTML = `
            <h2>Error Loading Kconfig</h2>
            <p>${error.message || error}</p>
        `;
  }
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

  nodes.forEach((node) => {
    const li = createNodeElement(node);
    if (li) ul.appendChild(li);
  });

  parentElement.appendChild(ul);
}

function createNodeElement(node) {
  const li = document.createElement("li");
  li.className = "tree-item";

  const nodeType = Object.keys(node)[0];
  const data = node[nodeType];
  const children = data.children || [];
  const isExpandable = children.length > 0;

  let content = "";
  switch (nodeType) {
    case "Config":
      li.dataset.name = data.name;
      content = `<span class="config-prompt">${data.prompt}</span> <span class="config-value">(${data.value})</span>`;
      break;
    case "Menu":
      content = `<strong class="menu-title">${data.prompt}</strong>`;
      break;
    case "Choice":
      content = `<strong class="choice-title">Choice: ${data.prompt}</strong>`;
      break;
    case "Comment":
      content = `<i class="comment-text">${data}</i>`;
      break;
    default:
      return null;
  }

  const nodeDiv = document.createElement("div");
  nodeDiv.className = "tree-node";
  nodeDiv.innerHTML = `<span class="toggle">${isExpandable ? "▶" : ""}</span> ${content}`;
  li.appendChild(nodeDiv);

  if (isExpandable) {
    const childrenUl = document.createElement("ul");
    childrenUl.className = "children collapsed";
    renderTree(children, childrenUl);
    li.appendChild(childrenUl);
  }

  nodeDiv.addEventListener("click", (e) => {
    e.stopPropagation();
    if (isExpandable) {
      const childrenUl = li.querySelector(".children");
      childrenUl.classList.toggle("collapsed");
      nodeDiv.querySelector(".toggle").textContent =
        childrenUl.classList.contains("collapsed") ? "▶" : "▼";
    }
    if (nodeType === "Config") {
      document
        .querySelectorAll(".tree-node.selected")
        .forEach((n) => n.classList.remove("selected"));
      nodeDiv.classList.add("selected");
      displayOptionDetails(data.name);
    }
  });

  return li;
}

// --- Details Pane Logic ---
async function displayOptionDetails(name) {
  const detailsContainer = document.getElementById("configDetails");
  try {
    const option = await invoke("get_kconfig_option", { name });
    if (!option) {
      detailsContainer.innerHTML = `<p>Could not retrieve details for ${name}.</p>`;
      return;
    }
    selectedOption = option;

    let valueControls = "";
    switch (option.type) {
      case "bool":
        valueControls = `
                    <label><input type="radio" name="${option.name}" value="y" ${option.value === "y" ? "checked" : ""}> Yes</label>
                    <label><input type="radio" name="${option.name}" value="n" ${option.value === "n" ? "checked" : ""}> No</label>
                `;
        break;
      case "tristate":
        valueControls = `
                    <label><input type="radio" name="${option.name}" value="y" ${option.value === "y" ? "checked" : ""}> Yes</label>
                    <label><input type="radio" name="${option.name}" value="m" ${option.value === "m" ? "checked" : ""}> Module</label>
                    <label><input type="radio" name="${option.name}" value="n" ${option.value === "n" ? "checked" : ""}> No</label>
                `;
        break;
      case "string":
        valueControls = `<input type="text" id="stringValue" value="${option.value.replace(/"/g, "&quot;")}">`;
        break;
      case "int":
      case "hex":
        valueControls = `<input type="text" id="numericalValue" value="${option.value}">`;
        break;
    }

    detailsContainer.innerHTML = `
            <h2>${option.name}</h2>
            <p><strong>Prompt:</strong> ${option.prompt}</p>
            <p><strong>Type:</strong> ${option.type}</p>
            <div class="value-controls">
                <strong>Value:</strong> ${valueControls}
            </div>
            ${option.depends ? `<p><strong>Depends on:</strong> <code>${option.depends}</code></p>` : ""}
            <div class="help-text">
                <h3>Help</h3>
                <pre>${option.help || "No help text available."}</pre>
            </div>
        `;

    detailsContainer.querySelectorAll("input").forEach((input) => {
      const eventType =
        input.type === "radio" || input.type === "checkbox"
          ? "change"
          : "input";
      input.addEventListener(eventType, handleValueChange);
    });
  } catch (error) {
    console.error(`Failed to get option ${name}:`, error);
    detailsContainer.innerHTML = `<p>Error fetching details for ${name}: ${error.message || error}</p>`;
  }
}

// --- Backend Communication ---
async function handleValueChange(e) {
  if (!selectedOption) return;
  const { name } = selectedOption;
  let value = e.target.value;

  try {
    const success = await invoke("set_kconfig_option", { name, value });
    if (success) {
      const treeNodeValue = document.querySelector(
        `li[data-name="${name}"] .config-value`,
      );
      if (treeNodeValue) treeNodeValue.textContent = `(${value})`;
      selectedOption.value = value; // Keep state in sync
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
        (data.prompt && data.prompt.toLowerCase().includes(searchTerm)) ||
        (data.name && data.name.toLowerCase().includes(searchTerm));

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
document.addEventListener("DOMContentLoaded", initializeApp);
