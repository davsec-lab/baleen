use std::{
    io::{BufRead, BufReader},
    process::{Command, Stdio},
};

use clap::Parser;

#[derive(Parser)]
struct Args {
    /// The output file path.
    #[arg(short, long)]
    output: String,
}

fn main() {
    let args = Args::parse();

    let output = std::path::absolute(&args.output).unwrap();

    let vars = vec![
        ("BFFF_LOG_LEVEL", "DEBUG"),
        ("BFFF_OUTPUT", output.to_str().unwrap()),
    ];

    let mut command = Command::new("cargo")
        .arg("check")
        .arg("--keep-going")
        .env("RUSTC_WRAPPER", "bfff-driver")
        .envs(vars)
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();

    if let Some(stdout) = command.stdout.take() {
        let stdout_reader = BufReader::new(stdout);
        std::thread::spawn(move || {
            stdout_reader.lines().for_each(|line| {
                if let Ok(line) = line {
                    println!("{}", line);
                }
            });
        });
    }

    let status = command.wait().unwrap();

    if !status.success() {
        eprintln!("Failed with exit code: {:?}", status.code());
    }
}
