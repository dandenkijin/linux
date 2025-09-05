# Makefile for wconf-app development

# Default target
.PHONY: dev

dev:
	@echo "Starting wconf development server..."
	npm run dev

# Clean build artifacts
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	cargo clean
	rm -rf node_modules/
	rm -f package-lock.json
	rm -rf dist/

# Install dependencies
.PHONY: deps
deps:
	npm install

# Build frontend assets for production
.PHONY: assets
assets:
	npm run build:prod

# Build release version
.PHONY: release
release: assets
	cargo build --release

# Run tests
.PHONY: test
test:
	cargo test

# Format code
.PHONY: fmt
fmt:
	cargo fmt --all
	npx prettier --write .

# Lint code
.PHONY: lint
lint:
	cargo clippy -- -D warnings
	npx eslint . --ext .js,.jsx,.ts,.tsx

# Run all checks (lint + test)
.PHONY: check
check: lint test

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  dev      - Start development server"
	@echo "  release  - Build release version"
	@echo "  test     - Run tests"
	@echo "  lint     - Run linters"
	@echo "  check    - Run all checks (lint + test)"
	@echo "  fmt      - Format code"
	@echo "  clean    - Remove build artifacts"
	@echo "  deps     - Install dependencies"
	@echo "  help     - Show this help"
