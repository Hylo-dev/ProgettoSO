# ProgettoSO

A C-based project for managing a cafeteria/restaurant system with clients, workers, and queues.

## Project Structure

- `src/` - Source code files
- `test/` - Test files
- `libds/` - External library (if present)
- `config.json` - Configuration file
- `menu.json` - Menu configuration

## Building the Project

```bash
# Build all executables
make all

# Build production version (optimized)
make production

# Clean build artifacts
make clean
```

## Running the Project

```bash
# Run main executable
make run-main

# Run worker executable
make run-worker

# Run client executable
make run-client
```

## Viewing Commit History

Here are various ways to view the commit history of this repository:

### Basic commit history
```bash
git log
```
This shows the full commit history with details including commit hash, author, date, and commit message.

### Compact one-line format
```bash
git log --oneline
```
Shows a condensed view with abbreviated commit hashes and messages.

### View last N commits
```bash
git log -n 10        # Last 10 commits
git log --oneline -5  # Last 5 commits in compact format
```

### Graphical representation
```bash
git log --graph --oneline --all
```
Shows a text-based graph of the commit history including all branches.

### Detailed graph with decorations
```bash
git log --graph --oneline --decorate --all
```
Includes branch and tag names in the graph view.

### View commits by author
```bash
git log --author="Author Name"
```

### View commits with file changes
```bash
git log --stat        # Shows files changed in each commit
git log -p            # Shows full diff for each commit
git log -p filename   # Shows changes to a specific file
```

### View commits in a date range
```bash
git log --since="2024-01-01"
git log --after="2 weeks ago"
git log --until="2024-12-31"
```

### Pretty formatted output
```bash
git log --pretty=format:"%h - %an, %ar : %s"
```
Custom format showing: hash - author, relative date : message

### View commit history for a specific file
```bash
git log -- filename
git log --follow -- filename  # Follow renames
```

### Interactive tools
```bash
gitk            # Graphical history viewer (if installed)
git log --all --graph --decorate --oneline --simplify-by-decoration
```

## License

See LICENSE file for details.
