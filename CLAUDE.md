# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

### xv6

- `make qemu`: Build and run xv6 in QEMU.
- `make clean`: Clean the build artifacts.
- `make grade`: Run the grading script for the current lab.
- `make qemu-gdb`: Build and run xv6 in QEMU with a GDB server.

## Code Architecture

This repository contains the xv6-riscv operating system, a small, Unix-like teaching operating system for RISC-V.

- `kernel/`: The source code for the xv6 operating system kernel.
- `user/`: The source code for user-level programs like `sh`, `ls`, and `cat`.
- `mkfs/`: A host program to create the initial file system image (`fs.img`).
