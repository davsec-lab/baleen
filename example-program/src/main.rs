unsafe extern "C" {
    fn foo(any: *mut i32) -> *mut i32;
}

fn main() {
    println!("Hello, world!");

    let mut value = Box::new(5);

    unsafe {
        let returned = foo(&mut *value as *mut i32);

        *returned = 10;
        *returned = 11;
        *returned = 12;
    }
}
