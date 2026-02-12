# Quick Start: Remote Development Setup

This is a quick reference for setting up remote development with VSCode. For detailed instructions, see [REMOTE_DEVELOPMENT.md](REMOTE_DEVELOPMENT.md).

## 5-Minute Setup

### 1. Install VSCode Extensions (Local Machine)
```
- Remote - SSH
- GitHub Copilot (optional)
- C/C++
```

### 2. Configure SSH
Create/edit `~/.ssh/config`:
```
Host my-remote
    HostName <your-ip-or-hostname>
    User <your-username>
    IdentityFile ~/.ssh/id_rsa
    ServerAliveInterval 60
```

See [ssh-config-example](ssh-config-example) for a complete template.

### 3. Connect
1. Open VSCode
2. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac)
3. Type: `Remote-SSH: Connect to Host`
4. Select `my-remote`

### 4. Open Project
In the remote window:
- File → Open Folder
- Navigate to rt-profiler directory
- Click OK

### 5. Install Remote Extensions
When prompted, install these on remote:
- GitHub Copilot
- C/C++
- Python

## Common Commands

### Building on Remote
```bash
# Build GCC plugin
g++ -shared -fPIC -o ./gcc_plugin/line_instrument_plugin.so \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/line_instrument_plugin.cc -fno-rtti -O2

# Build runtime
gcc -c -O0 -g ./runtime/rt_record.c -o ./runtime/rt_record.o
```

### Testing on Remote
```bash
./tools/run_all.sh ./your_program.c 1
```

## Troubleshooting

**Can't connect?**
- Test SSH: `ssh my-remote`
- Check firewall/network
- Verify SSH service is running

**Copilot not working?**
- Sign in: Ctrl+Shift+P → "GitHub Copilot: Sign In"
- Verify extension is installed remotely
- Restart VSCode window

**Build errors?**
- Check GCC version: `gcc --version`
- Verify plugin support: `gcc -print-file-name=plugin`

## Tips

✅ Use SSH keys (not passwords) for seamless connection  
✅ Keep your SSH session alive with ServerAliveInterval  
✅ Install Copilot on the remote machine for best performance  
✅ Use `.gitignore` to exclude build artifacts  
✅ Consider tmux/screen for long-running builds

## Next Steps

- Read the full [Remote Development Guide](REMOTE_DEVELOPMENT.md)
- Configure your SSH with the [example config](ssh-config-example)
- Customize VSCode settings in `.vscode/settings.json`
