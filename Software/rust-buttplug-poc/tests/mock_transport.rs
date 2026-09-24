//! Only the ESP32 boundary is replaced; controller, upstream client and renderer are real.
use std::sync::{
    Arc,
    atomic::{AtomicBool, AtomicU32, Ordering},
};
use tokio::sync::oneshot;
#[derive(Clone, Debug)]
pub struct Esp32BleCandidate {
    pub name: String,
    pub display_name: String,
    pub address: String,
    pub protocols: Vec<String>,
}
pub struct DiscoverySnapshot {
    pub candidates: Vec<Esp32BleCandidate>,
    pub complete: bool,
}
#[derive(Clone, Default)]
pub struct Esp32BleCandidateApprover {
    pub generation: Arc<AtomicU32>,
    pub live: Arc<AtomicBool>,
}
impl Esp32BleCandidateApprover {
    pub fn approve(&self, _: &str) -> Result<u32, String> {
        Ok(self.generation.fetch_add(1, Ordering::SeqCst) + 1)
    }
    pub fn pause_scan(&self) {}
    pub fn allow_scan(&self) {}
    pub fn has_live_connection(&self, generation: u32) -> bool {
        self.generation.load(Ordering::SeqCst) == generation && self.live.load(Ordering::SeqCst)
    }
    pub fn cancel(&self) -> oneshot::Receiver<Result<(), String>> {
        self.generation.fetch_add(1, Ordering::SeqCst);
        self.live.store(false, Ordering::SeqCst);
        let (tx, rx) = oneshot::channel();
        let _ = tx.send(Ok(()));
        rx
    }
}
