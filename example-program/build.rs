fn main() {
    cc::Build::new().file("library.c").compile("library");

    println!("cargo:rerun-if-changed=library.c");
}
