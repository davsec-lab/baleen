<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="README/BaleenDark.png">
    <source media="(prefers-color-scheme: light)" srcset="README/BaleenLight.png">
    <img alt="Baleen" src="README/BaleenLight.png" style="width:200px;">
  </picture>
</div>

Track memory allocation and access between C and Rust code in polyglot programs.

## Getting Started

1. Install Intel Pin and Rust
2. Clone this repository into the `source/tools` folder of your Pin installation
3. Run `build.sh` to build Baleen and follow the instructions to add it to your PATH
4. Copy the `rust-toolchain.toml` file from the example program and paste it into the Rust crate you want to analyze
5. In your Rust project, run `cargo build` to generate the binary executable
6. Run `baleen <PATH TO EXECUTABLE>` and watch the results in the freshly created `.baleen` directory

## Example
When you run Baleen on the example program, the final report (`.baleen/report.txt`) will look something like this.

```txt
--- Allocation Report ---
Rust:   2704 bytes
C:      1152 bytes
Total:  3856 bytes

Name, Reads (Rust), Reads (C), Writes (Rust), Writes (C)
0, 1249, 0, 324, 0
1, 6114, 0, 1091, 0
2, 1934, 0, 0, 0
3, 2, 0, 2, 0
4, 0, 0, 2, 0
5, 0, 0, 0, 0
6, 0, 0, 0, 30
7, 0, 0, 3, 2
```

The second portion of this file is in CSV format. In the future, a separate CSV file will be generated. For now, copy and paste it into a separate file if you'd like to open it in Excel. This table contains a list of every object allocated by your program, as well as the number of reads and writes that touch these objects.

Our program is pretty simple, so we can mostly figure out which object is which. For example, object `7` is the `something` object allocated in `library.c` because Rust writes to it three times and C writes to it twice.

You can also name objects you're interested in tracking using the `baleen` marker function.

```rs
use std::arch::asm;

#[unsafe(no_mangle)]
#[inline(never)]
pub extern "C" fn baleen(ptr: *const u8, size: usize, name: *const u8) {
    unsafe { asm!("nop", options(nomem, nostack, preserves_flags)); }
}
```

In the future, this function will be provided by a crate.