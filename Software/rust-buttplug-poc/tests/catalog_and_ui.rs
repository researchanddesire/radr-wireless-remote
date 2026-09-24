use crate::{
    device_names::{CatalogName, friendly_name},
    runtime_state,
    screen::{RadBleCandidateSummary, RadBleControlSetting},
    ui,
};
use embedded_graphics::{pixelcolor::Rgb565, prelude::*};

#[test]
fn names_resolve_from_the_pinned_official_catalog() {
    let config = buttplug_server_device_config::load_protocol_configs(&None, &None, false)
        .unwrap()
        .finish()
        .unwrap();
    let names: Vec<_> = config
        .base_device_definitions()
        .iter()
        .map(|(id, definition)| CatalogName {
            protocol: id.protocol().clone(),
            identifier: id.identifier().clone(),
            name: definition.name().clone(),
        })
        .collect();
    assert_eq!(
        friendly_name("LVS-Domi39", &["lovense".into()], &names),
        "Lovense Domi"
    );
    assert_eq!(
        friendly_name("XHT", &["mizzzee-v2".into()], &names),
        "Mizz Zee Device"
    );
    for name in &names {
        if let Some(identifier) = &name.identifier {
            let label = friendly_name(identifier, &[name.protocol.clone()], &names);
            assert!(!label.is_empty());
        }
    }
}

#[test]
fn render_full_list_and_all_connection_states() {
    let state = runtime_state::shared_state();
    runtime_state::with_state(&state, |state| {
        state.phase = "searching".into();
        state.candidates = (0..1000)
            .map(|index| RadBleCandidateSummary {
                name: format!("Device {index}"),
                advertised_name: format!("BLE-{index}"),
                address: format!("00:00:00:{index:06}"),
                protocols: vec![],
            })
            .collect();
        state.candidate_count = 1000;
        state.selected_index = Some(999);
    });
    let search = ui::render(&runtime_state::screen_state(&state));
    assert_eq!(search.pixels.len(), 320 * 240);
    assert!(
        search
            .pixels
            .iter()
            .all(|pixel| *pixel == Rgb565::BLACK || *pixel == Rgb565::WHITE)
    );
    if let Ok(directory) = std::env::var("RADR_PREVIEW_DIR") {
        std::fs::create_dir_all(&directory).unwrap();
        for (phase, file) in [
            ("starting", "boot"),
            ("searching", "devices"),
            ("connecting", "connecting"),
            ("connected", "controls"),
            ("reconnecting", "reconnecting"),
            ("error", "error"),
        ] {
            runtime_state::with_state(&state, |state| {
                state.phase = phase.into();
                state.selected_name = if phase == "starting" {
                    None
                } else {
                    Some("Lovense Domi".into())
                };
                state.elapsed_seconds = 12;
                state.status = if phase == "starting" {
                    "Getting ready"
                } else {
                    "Identifying device and loading controls"
                }
                .into();
                state.candidates = vec![
                    RadBleCandidateSummary {
                        name: "Lovense Domi".into(),
                        advertised_name: "LVS-Domi39".into(),
                        address: "00:11:22:33:44:55".into(),
                        protocols: vec![],
                    },
                    RadBleCandidateSummary {
                        name: "Lelo F1s".into(),
                        advertised_name: "F1s".into(),
                        address: "00:11:22:33:44:56".into(),
                        protocols: vec![],
                    },
                ];
                state.selected_index = Some(0);
                state.connected_name = if matches!(phase, "connected" | "reconnecting") {
                    Some("Lovense Domi".into())
                } else {
                    None
                };
                state.controls = vec![RadBleControlSetting {
                    feature_index: 0,
                    label: "Vibration".into(),
                    output_type: "Vibrate".into(),
                    value_percent: 25,
                    minimum_percent: 0,
                }];
                if phase == "error" {
                    state.error_code = Some("E203".into());
                    state.status = "Device setup timed out".into();
                }
            });
            let frame = ui::render(&runtime_state::screen_state(&state));
            let mut bytes = b"P6\n320 240\n255\n".to_vec();
            for pixel in frame.pixels {
                bytes.extend([
                    (u16::from(pixel.r()) * 255 / 31) as u8,
                    (u16::from(pixel.g()) * 255 / 63) as u8,
                    (u16::from(pixel.b()) * 255 / 31) as u8,
                ]);
            }
            std::fs::write(format!("{directory}/{file}.ppm"), bytes).unwrap();
        }
    }
}
