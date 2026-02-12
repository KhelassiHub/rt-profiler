# Remote Development with VSCode and GitHub Copilot

This guide explains how to work on the rt-profiler project using SSH remote development in Visual Studio Code while leveraging GitHub Copilot running in the cloud or on another machine.

## Overview

Remote development with VSCode allows you to:
- Use VSCode on your local machine as a frontend
- Connect to a remote machine (like a Raspberry Pi 3) via SSH
- Run GitHub Copilot either locally or on a separate machine/cloud instance
- Have all the compute-intensive operations happen on the remote machine

## Prerequisites

1. **Local Machine (Your Computer):**
   - Visual Studio Code installed
   - GitHub Copilot extension (optional - can run on remote)
   - SSH client configured

2. **Remote Machine (Target Development Environment):**
   - SSH server running and accessible
   - GCC with plugin support
   - Build tools (gcc, g++)
   - Python 3 (for tools)

## Setup Instructions

### 1. Install Required VSCode Extensions

On your local VSCode, install:
- `Remote - SSH` (ms-vscode-remote.remote-ssh)
- `Remote - SSH: Editing Configuration Files` (ms-vscode-remote.remote-ssh-edit)
- `GitHub Copilot` (github.copilot) - Optional: can be installed remotely
- `GitHub Copilot Chat` (github.copilot-chat) - Optional: can be installed remotely
- `C/C++` (ms-vscode.cpptools)

### 2. Configure SSH Connection

1. Open VSCode Command Palette (`Ctrl+Shift+P` or `Cmd+Shift+P`)
2. Type "Remote-SSH: Add New SSH Host"
3. Enter your SSH connection command:
   ```
   ssh user@remote-host-ip
   ```
   Example for Raspberry Pi:
   ```
   ssh pi@192.168.1.100
   ```

4. Select the SSH config file to update (usually `~/.ssh/config`)

### 3. Connect to Remote Host

1. Open Command Palette again
2. Type "Remote-SSH: Connect to Host"
3. Select your configured remote host
4. A new VSCode window will open connected to the remote machine

### 4. Open the Project on Remote Machine

1. In the remote VSCode window: File → Open Folder
2. Navigate to where you've cloned rt-profiler
3. VSCode will now show the remote file system

### 5. Install Extensions on Remote

VSCode will prompt you to install recommended extensions on the remote machine. Click "Install All" or install individually:
- GitHub Copilot (for AI assistance)
- C/C++ (for language support)
- Python (for tools support)

## GitHub Copilot Configuration Options

### Option 1: Copilot on Local Machine Only
- Install Copilot extension locally only
- Copilot processes happen on your local machine
- Suggestions are sent to remote VSCode session
- **Pros:** No setup needed on remote
- **Cons:** May have slight latency

### Option 2: Copilot on Remote Machine
- Install Copilot extension on remote VSCode Server
- Copilot runs directly on the remote machine
- **Pros:** Better integration, lower latency
- **Cons:** Requires GitHub authentication on remote

### Option 3: Copilot on Separate Cloud/Machine
- Set up a dedicated VSCode Server on cloud instance
- Install Copilot there
- Connect from your local VSCode to the cloud instance
- **Pros:** Offload compute from both local and target remote
- **Cons:** More complex setup, additional costs

## Building the Project Remotely

Once connected via SSH, all commands run on the remote machine:

```bash
# Build the GCC plugin
g++ -shared -fPIC -o ./gcc_plugin/line_instrument_plugin.so \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/line_instrument_plugin.cc -fno-rtti -O2

# Build runtime object
gcc -c -O0 -g ./runtime/rt_record.c -o ./runtime/rt_record.o

# Run full sweep
chmod +x ./tools/run_all.sh
chmod +x ./tools/next_target.py
./tools/run_all.sh ./your_program.c 1
```

## Tips and Best Practices

1. **SSH Key Authentication:** Set up SSH keys instead of passwords for seamless connection
   ```bash
   ssh-copy-id user@remote-host
   ```

2. **Keep Alive:** Add to your SSH config to prevent disconnections:
   ```
   Host remote-pi
       HostName 192.168.1.100
       User pi
       ServerAliveInterval 60
       ServerAliveCountMax 3
   ```

3. **Port Forwarding:** Forward ports if you need to access services:
   ```
   Host remote-pi
       LocalForward 8080 localhost:8080
   ```

4. **Resource Management:** Monitor remote machine resources:
   ```bash
   htop  # or top
   df -h  # disk space
   ```

5. **Copilot Performance:** 
   - Use Copilot's inline suggestions for quick edits
   - Use Copilot Chat for complex questions
   - Disable Copilot on slow connections if needed

## Troubleshooting

### Connection Issues
- Verify SSH service is running on remote: `sudo systemctl status ssh`
- Check firewall rules: `sudo ufw status`
- Test basic SSH connection: `ssh user@host`

### Copilot Not Working
- Check authentication: Command Palette → "GitHub Copilot: Sign In"
- Verify extension is installed and enabled
- Check network connectivity
- Restart VSCode window

### Build Issues
- Verify GCC plugin support: `gcc -print-file-name=plugin`
- Check GCC version: `gcc --version`
- Ensure all dependencies are installed

### Performance Issues
- Close unused extensions on remote
- Reduce file watchers if needed
- Use `.gitignore` to exclude build artifacts
- Consider using tmux/screen for long-running builds

## Advanced: Multi-Machine Setup

For optimal performance with multiple machines:

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   Local     │   SSH   │    Cloud     │   SSH   │  Raspberry  │
│   Machine   │────────>│   VSCode     │────────>│   Pi 3      │
│  (Frontend) │         │   Server     │         │  (Target)   │
│             │         │  + Copilot   │         │             │
└─────────────┘         └──────────────┘         └─────────────┘
```

1. Set up VSCode Server on cloud instance (e.g., AWS, Azure, GCP)
2. Install GitHub Copilot on cloud instance
3. SSH from cloud to Raspberry Pi for building/testing
4. Connect your local VSCode to cloud instance

## Additional Resources

- [VSCode Remote Development](https://code.visualstudio.com/docs/remote/remote-overview)
- [Remote SSH Documentation](https://code.visualstudio.com/docs/remote/ssh)
- [GitHub Copilot Documentation](https://docs.github.com/en/copilot)
- [VSCode Server](https://code.visualstudio.com/docs/remote/vscode-server)

## Security Considerations

- Use SSH keys instead of passwords
- Keep your SSH keys secure and use passphrases
- Regularly update remote system packages
- Use firewall rules to limit SSH access
- Consider using VPN for additional security
- Don't commit SSH config or credentials to git
