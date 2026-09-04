use serde::Serialize;

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RadBleCandidateSummary {
    pub name: String,
    pub advertised_name: String,
    pub address: String,
    pub protocols: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RadBleControlSetting {
    pub feature_index: u32,
    pub label: String,
    pub output_type: String,
    pub value_percent: i16,
    pub minimum_percent: i16,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RadBleScreenState {
    pub phase: String,
    pub status: String,
    pub error_code: Option<String>,
    pub elapsed_seconds: u64,
    pub connection_generation: u32,
    pub candidate_count: usize,
    pub selected_index: Option<usize>,
    pub selected_name: Option<String>,
    pub selected_protocols: Vec<String>,
    pub candidates: Vec<RadBleCandidateSummary>,
    pub connected_name: Option<String>,
    pub feature_count: usize,
    pub selected_control_index: Option<usize>,
    pub controls: Vec<RadBleControlSetting>,
    pub control_revision: u32,
    pub last_error: Option<String>,
}
