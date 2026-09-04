fn main() {
    println!("cargo:rerun-if-env-changed=RADR_BUTTPLUG_AUTO_APPROVE_NAME");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("espidf") {
        embuild::espidf::sysenv::output();
    }
}
