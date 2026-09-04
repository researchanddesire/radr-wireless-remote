fn main() {
    println!("cargo:rerun-if-env-changed=RADR_BUTTPLUG_AUTO_APPROVE_NAME");
    embuild::espidf::sysenv::output();
}
