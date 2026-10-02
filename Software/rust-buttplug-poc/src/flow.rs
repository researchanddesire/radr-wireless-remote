//! Hardware-independent lifecycle policy. The controller executes its effects.
use std::time::{Duration, Instant};

pub const SEARCH_INTERVAL: Duration = Duration::from_secs(10);
pub const INTERACTION_HOLD: Duration = Duration::from_secs(5);
pub const CONNECT_TIMEOUT: Duration = Duration::from_secs(45);
pub const MAX_RECONNECT_ATTEMPTS: u8 = 3;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    Searching,
    Menu,
    Connecting,
    Connected,
    Reconnecting,
    Disconnecting,
    Error,
}

impl Phase {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Searching => "searching",
            Self::Menu => "menu",
            Self::Connecting => "connecting",
            Self::Connected => "connected",
            Self::Reconnecting => "reconnecting",
            Self::Disconnecting => "disconnecting",
            Self::Error => "error",
        }
    }
    pub fn waiting(self) -> bool {
        matches!(self, Self::Connecting | Self::Reconnecting)
    }
}

pub struct Flow {
    pub phase: Phase,
    pub attempt: u8,
    pub entered: Instant,
    last_interaction: Instant,
    last_search: Option<Instant>,
}

impl Flow {
    pub fn new(now: Instant) -> Self {
        Self {
            phase: Phase::Searching,
            attempt: 0,
            entered: now,
            last_interaction: now - INTERACTION_HOLD,
            last_search: None,
        }
    }
    pub fn enter(&mut self, phase: Phase, now: Instant) {
        self.phase = phase;
        self.entered = now;
        if phase == Phase::Searching {
            self.last_search = None;
        }
    }
    pub fn interact(&mut self, now: Instant) {
        self.last_interaction = now;
    }
    pub fn browsing(&self, now: Instant) -> bool {
        now.duration_since(self.last_interaction) < INTERACTION_HOLD
    }
    pub fn search_due(&self, now: Instant) -> bool {
        self.phase == Phase::Searching
            && !self.browsing(now)
            && self
                .last_search
                .is_none_or(|last| now.duration_since(last) >= SEARCH_INTERVAL)
    }
    pub fn resume_search(&mut self, now: Instant) {
        self.last_interaction = now - INTERACTION_HOLD;
        self.last_search = None;
    }

    pub fn searched(&mut self, now: Instant) {
        self.last_search = Some(now);
    }
    pub fn begin_connection(&mut self, now: Instant) {
        self.attempt = 0;
        self.enter(Phase::Connecting, now);
    }
    pub fn connection_ready(&mut self, now: Instant) -> bool {
        if !self.phase.waiting() {
            return false;
        }
        self.enter(Phase::Connected, now);
        true
    }
    pub fn connection_lost(&mut self, now: Instant) -> bool {
        if self.phase != Phase::Connected {
            return false;
        }
        self.attempt = 0;
        self.enter(Phase::Reconnecting, now);
        true
    }
    pub fn timed_out(&self, now: Instant) -> bool {
        self.phase.waiting() && now.duration_since(self.entered) >= CONNECT_TIMEOUT
    }
    pub fn retry(&mut self, now: Instant) -> bool {
        if self.phase != Phase::Reconnecting || self.attempt >= MAX_RECONNECT_ATTEMPTS {
            return false;
        }
        self.attempt += 1;
        self.entered = now;
        true
    }
}

/// Preserve every encoder detent and wrap in both directions, including large deltas.
pub fn wrap_selection(selected: usize, delta: i32, count: usize) -> usize {
    if count == 0 {
        return 0;
    }
    ((selected as i128 + i128::from(delta)).rem_euclid(count as i128)) as usize
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn discovery_only_runs_while_searching_and_idle() {
        let now = Instant::now();
        let mut flow = Flow::new(now);
        assert!(flow.search_due(now));
        flow.searched(now);
        assert!(!flow.search_due(now + Duration::from_secs(9)));
        flow.interact(now + Duration::from_secs(9));
        assert!(!flow.search_due(now + Duration::from_secs(10)));
        assert!(flow.search_due(now + Duration::from_secs(14)));
        for phase in [
            Phase::Menu,
            Phase::Connecting,
            Phase::Connected,
            Phase::Reconnecting,
            Phase::Disconnecting,
            Phase::Error,
        ] {
            flow.enter(phase, now);
            assert!(!flow.search_due(now + Duration::from_secs(100)));
        }
    }
    #[test]
    fn cancelled_connections_cannot_enable_controls() {
        let now = Instant::now();
        let mut flow = Flow::new(now);
        flow.begin_connection(now);
        flow.enter(Phase::Disconnecting, now);
        assert!(!flow.connection_ready(now));
        flow.enter(Phase::Searching, now);
        assert!(!flow.connection_ready(now));
    }
    #[test]
    fn reconnect_is_bounded_and_requires_a_confirmed_connection() {
        let now = Instant::now();
        let mut flow = Flow::new(now);
        assert!(!flow.connection_lost(now));
        flow.begin_connection(now);
        assert!(flow.connection_ready(now));
        assert!(flow.connection_lost(now));
        for attempt in 1..=MAX_RECONNECT_ATTEMPTS {
            assert!(flow.retry(now));
            assert_eq!(flow.attempt, attempt);
            assert!(!flow.timed_out(now + CONNECT_TIMEOUT - Duration::from_secs(1)));
            assert!(flow.timed_out(now + CONNECT_TIMEOUT));
        }
        assert!(!flow.retry(now));
    }
    #[test]
    fn selection_wraps_without_a_page_limit_or_integer_overflow() {
        assert_eq!(wrap_selection(0, -1, 1000), 999);
        assert_eq!(wrap_selection(999, 1, 1000), 0);
        assert_eq!(wrap_selection(0, 17, 1000), 17);
        assert_eq!(wrap_selection(0, i32::MIN, 3), 1);
        assert_eq!(wrap_selection(0, 1, 0), 0);
    }
}

/// A stop or cancellation supersedes older output completions, regardless of arrival order.
#[derive(Default)]
pub struct CommandSequence(u64);
impl CommandSequence {
    pub fn advance(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(1);
        self.0
    }
    pub fn accepts(&self, sequence: u64) -> bool {
        sequence == self.0
    }
}
#[cfg(test)]
mod command_tests {
    use super::*;
    #[test]
    fn late_output_cannot_restore_a_value_after_stop() {
        let mut commands = CommandSequence::default();
        let output = commands.advance();
        let stop = commands.advance();
        assert!(!commands.accepts(output));
        assert!(commands.accepts(stop));
        commands.advance();
        assert!(!commands.accepts(stop));
    }
}
