//! Discovery labels are hints; only upstream protocol identification establishes a model.
#[derive(Clone)]
pub struct CatalogName {
    pub protocol: String,
    pub identifier: Option<String>,
    pub name: String,
}

pub fn friendly_name(advertised: &str, protocols: &[String], catalog: &[CatalogName]) -> String {
    let relevant: Vec<_> = catalog
        .iter()
        .filter(|entry| protocols.contains(&entry.protocol))
        .collect();
    // Many upstream identifiers are the advertisement itself. Require a unique name.
    let exact: Vec<_> = relevant
        .iter()
        .filter(|entry| entry.identifier.as_deref() == Some(advertised))
        .map(|entry| entry.name.as_str())
        .collect();
    if let Some(name) = unique(&exact) {
        return name.to_owned();
    }
    if protocols.iter().any(|protocol| protocol == "lovense")
        && let Some(model) = advertised.strip_prefix("LVS-")
    {
        // Suffix digits are frequently an address/firmware suffix, NOT a model generation.
        let model = model
            .trim_end_matches(|c: char| c.is_ascii_digit())
            .trim_end_matches('-');
        let matches: Vec<_> = relevant
            .iter()
            .filter(|entry| entry.protocol == "lovense")
            .filter(|entry| {
                entry
                    .identifier
                    .as_deref()
                    .is_some_and(|id| id.eq_ignore_ascii_case(model))
                    || entry
                        .name
                        .strip_prefix("Lovense ")
                        .is_some_and(|name| name.eq_ignore_ascii_case(model))
            })
            .map(|entry| entry.name.as_str())
            .collect();
        if let Some(name) = unique(&matches) {
            return name.to_owned();
        }
    }
    // A single-model protocol can be named before connecting; multi-model protocols cannot.
    let names: Vec<_> = relevant.iter().map(|entry| entry.name.as_str()).collect();
    if let Some(name) = unique(&names) {
        return name.to_owned();
    }
    if !advertised.trim().is_empty() {
        return advertised.to_owned();
    }
    let defaults: Vec<_> = relevant
        .iter()
        .filter(|entry| entry.identifier.is_none())
        .map(|entry| entry.name.as_str())
        .collect();
    unique(&defaults).unwrap_or("Bluetooth device").to_owned()
}

fn unique<'a>(names: &[&'a str]) -> Option<&'a str> {
    let first = *names.first()?;
    (!first.trim().is_empty() && names.iter().all(|name| *name == first)).then_some(first)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn entry(protocol: &str, id: Option<&str>, name: &str) -> CatalogName {
        CatalogName {
            protocol: protocol.into(),
            identifier: id.map(str::to_owned),
            name: name.into(),
        }
    }
    #[test]
    fn upstream_names_win_but_domi_generation_is_not_invented() {
        let catalog = vec![
            entry("lovense", Some("W"), "Lovense Domi"),
            entry("lovense", Some("W2"), "Lovense Domi 2"),
        ];
        assert_eq!(
            friendly_name("LVS-Domi39", &["lovense".into()], &catalog),
            "Lovense Domi"
        );
        assert_eq!(
            friendly_name("LVS-W296", &["lovense".into()], &catalog),
            "Lovense Domi"
        );
        assert_eq!(
            friendly_name("unknown", &["lovense".into()], &catalog),
            "unknown"
        );
    }
    #[test]
    fn exact_identifiers_single_models_and_ambiguous_matches() {
        let catalog = vec![
            entry("one", Some("XHT"), "Friendly name"),
            entry("two", None, "Another device"),
        ];
        assert_eq!(
            friendly_name("XHT", &["one".into()], &catalog),
            "Friendly name"
        );
        assert_eq!(
            friendly_name("abc", &["two".into()], &catalog),
            "Another device"
        );
        assert_eq!(
            friendly_name("abc", &["one".into(), "two".into()], &catalog),
            "abc"
        );
        assert_eq!(friendly_name("", &[], &catalog), "Bluetooth device");
    }
}
