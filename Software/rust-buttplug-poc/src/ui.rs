use crate::screen::RadBleScreenState;
use embedded_graphics::{
    mono_font::{
        MonoTextStyle,
        ascii::{FONT_6X10, FONT_10X20},
    },
    pixelcolor::Rgb565,
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, Rectangle},
    text::{Baseline, Text},
};
use std::convert::Infallible;
pub const WIDTH: u32 = 320;
pub const HEIGHT: u32 = 240;
pub struct FrameBuffer {
    pub pixels: Vec<Rgb565>,
}

impl FrameBuffer {
    fn new() -> Self {
        Self {
            pixels: vec![Rgb565::BLACK; (WIDTH * HEIGHT) as usize],
        }
    }
}

impl DrawTarget for FrameBuffer {
    type Color = Rgb565;
    type Error = Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(point, color) in pixels {
            if point.x >= 0 && point.y >= 0 && point.x < WIDTH as i32 && point.y < HEIGHT as i32 {
                let index = point.y as usize * WIDTH as usize + point.x as usize;
                self.pixels[index] = color;
            }
        }
        Ok(())
    }
}

impl OriginDimensions for FrameBuffer {
    fn size(&self) -> Size {
        Size::new(WIDTH, HEIGHT)
    }
}

fn clipped(value: &str, maximum: usize) -> String {
    let mut chars = value.chars();
    let mut text: String = chars
        .by_ref()
        .take(maximum)
        .map(|c| {
            if c.is_ascii() && !c.is_control() {
                c
            } else {
                ' '
            }
        })
        .collect();
    if chars.next().is_some() && maximum >= 3 {
        text.truncate(maximum - 3);
        text.push_str("...");
    }
    text
}
fn line(target: &mut FrameBuffer, value: &str, x: i32, y: i32, color: Rgb565) {
    let _ = Text::with_baseline(
        value,
        Point::new(x, y),
        MonoTextStyle::new(&FONT_6X10, color),
        Baseline::Top,
    )
    .draw(target);
}
fn title(target: &mut FrameBuffer, value: &str) {
    let _ = Text::with_baseline(
        &clipped(value, 30),
        Point::new(10, 37),
        MonoTextStyle::new(&FONT_10X20, Rgb565::WHITE),
        Baseline::Top,
    )
    .draw(target);
}
fn rect(target: &mut FrameBuffer, x: i32, y: i32, width: u32, height: u32, color: Rgb565) {
    let _ = Rectangle::new(Point::new(x, y), Size::new(width, height))
        .into_styled(PrimitiveStyle::with_fill(color))
        .draw(target);
}
fn header(target: &mut FrameBuffer, phase: &str) {
    let stroke = PrimitiveStyle::with_stroke(Rgb565::WHITE, 1);
    // Search, link, and controls: status icons represent actual lifecycle state.
    for (index, active) in [
        phase == "searching",
        matches!(phase, "connecting" | "reconnecting" | "connected"),
        phase == "connected",
    ]
    .iter()
    .enumerate()
    {
        let x = 104 + index as i32 * 44;
        if *active {
            rect(target, x - 4, 3, 28, 25, Rgb565::WHITE);
        }
        let ink = if *active {
            Rgb565::BLACK
        } else {
            Rgb565::WHITE
        };
        let style = PrimitiveStyle::with_stroke(ink, 1);
        match index {
            0 => {
                let _ = Circle::new(Point::new(x, 6), 12)
                    .into_styled(style)
                    .draw(target);
                let _ = Line::new(Point::new(x + 10, 16), Point::new(x + 17, 23))
                    .into_styled(style)
                    .draw(target);
            }
            1 => {
                let _ = Line::new(Point::new(x + 8, 5), Point::new(x + 8, 24))
                    .into_styled(style)
                    .draw(target);
                for (a, b) in [
                    ((8, 5), (15, 11)),
                    ((15, 11), (1, 21)),
                    ((1, 8), (15, 18)),
                    ((15, 18), (8, 24)),
                ] {
                    let _ = Line::new(Point::new(x + a.0, a.1), Point::new(x + b.0, b.1))
                        .into_styled(style)
                        .draw(target);
                }
            }
            _ => {
                for (dx, y) in [(2, 11), (9, 19), (16, 9)] {
                    let _ = Line::new(Point::new(x + dx, 6), Point::new(x + dx, 23))
                        .into_styled(style)
                        .draw(target);
                    rect(target, x + dx - 2, y, 5, 3, ink);
                }
            }
        }
    }
    let _ = Line::new(Point::new(8, 31), Point::new(311, 31))
        .into_styled(stroke)
        .draw(target);
}
fn footer(target: &mut FrameBuffer, left: &str, middle: &str, right: &str) {
    rect(target, 8, 211, 304, 1, Rgb565::WHITE);
    line(target, left, 10, 224, Rgb565::WHITE);
    line(
        target,
        middle,
        160 - (middle.len() as i32 * 3),
        224,
        Rgb565::WHITE,
    );
    line(
        target,
        right,
        310 - (right.len() as i32 * 6),
        224,
        Rgb565::WHITE,
    );
}
fn render_search(target: &mut FrameBuffer, screen: &RadBleScreenState) {
    title(target, "Devices");
    if screen.candidates.is_empty() {
        line(target, "Looking for devices...", 14, 93, Rgb565::WHITE);
        line(
            target,
            "Turn your device on and keep it nearby.",
            14,
            114,
            Rgb565::WHITE,
        );
    } else {
        let selected = screen
            .selected_index
            .unwrap_or(0)
            .min(screen.candidates.len() - 1);
        let start = selected.saturating_sub(4);
        for (row, (index, candidate)) in screen
            .candidates
            .iter()
            .enumerate()
            .skip(start)
            .take(5)
            .enumerate()
        {
            let y = 67 + row as i32 * 25;
            let active = index == selected;
            if active {
                rect(target, 8, y, 299, 23, Rgb565::WHITE);
            }
            let duplicate = screen
                .candidates
                .iter()
                .filter(|other| other.name == candidate.name)
                .count()
                > 1;
            let name = if duplicate {
                format!(
                    "{} ({})",
                    clipped(&candidate.name, 36),
                    candidate
                        .address
                        .chars()
                        .rev()
                        .take(5)
                        .collect::<String>()
                        .chars()
                        .rev()
                        .collect::<String>()
                )
            } else {
                clipped(&candidate.name, 47)
            };
            line(
                target,
                &name,
                13,
                y + 7,
                if active { Rgb565::BLACK } else { Rgb565::WHITE },
            );
        }
        if screen.error_code.is_none() {
            let counter = format!("{} / {}", selected + 1, screen.candidates.len());
            line(
                target,
                &counter,
                307 - counter.len() as i32 * 6,
                196,
                Rgb565::WHITE,
            );
        }
        // Viewport marker is proportional to the entire list, which has no page limit.
        rect(target, 311, 67, 1, 123, Rgb565::WHITE);
        rect(
            target,
            309,
            67 + (selected * 115 / screen.candidates.len().max(1)) as i32,
            4,
            8,
            Rgb565::WHITE,
        );
    }
    if let Some(code) = &screen.error_code {
        line(
            target,
            &format!("{code}  Search unavailable. Try again."),
            10,
            196,
            Rgb565::WHITE,
        );
    }
    footer(target, "MENU", "SEARCH AGAIN", "SELECT");
}
fn render_wait(target: &mut FrameBuffer, screen: &RadBleScreenState) {
    let label = match screen.phase.as_str() {
        "starting" => "RADR",
        "disconnecting" => "Disconnecting",
        "error" if screen.error_code.as_deref() == Some("E303") => "Disconnect failed",
        "error" => "Connection failed",
        _ => "Connecting",
    };
    title(target, label);
    line(
        target,
        &clipped(screen.selected_name.as_deref().unwrap_or(""), 48),
        12,
        72,
        Rgb565::WHITE,
    );
    line(target, &clipped(&screen.status, 48), 12, 103, Rgb565::WHITE);
    if screen.phase == "error" {
        line(
            target,
            screen.error_code.as_deref().unwrap_or("E205"),
            12,
            133,
            Rgb565::WHITE,
        );
        line(
            target,
            "Keep it nearby. Close other control apps.",
            12,
            154,
            Rgb565::WHITE,
        );
        footer(target, "BACK", "", "SEARCH AGAIN");
    } else {
        line(
            target,
            &format!("{}s", screen.elapsed_seconds),
            12,
            133,
            Rgb565::WHITE,
        );
        if screen.phase == "connecting" {
            line(
                target,
                "Some devices need extra time to get ready.",
                12,
                157,
                Rgb565::WHITE,
            );
            footer(target, "CANCEL", "", "");
        } else {
            footer(target, "", "", "");
        }
        let dots = ".".repeat((screen.elapsed_seconds % 4) as usize);
        line(target, &dots, 281, 133, Rgb565::WHITE);
    }
}
fn render_controls(target: &mut FrameBuffer, screen: &RadBleScreenState) {
    let connected = screen.phase == "connected";
    let ink = if connected {
        Rgb565::WHITE
    } else {
        Rgb565::new(16, 32, 16)
    };
    title(target, screen.connected_name.as_deref().unwrap_or("Device"));
    if screen.controls.is_empty() {
        line(target, "No adjustable outputs", 12, 91, ink);
    } else {
        let selected = screen
            .selected_control_index
            .unwrap_or(0)
            .min(screen.controls.len() - 1);
        for (row, (index, control)) in screen
            .controls
            .iter()
            .enumerate()
            .skip(selected.saturating_sub(3))
            .take(4)
            .enumerate()
        {
            let y = 65 + row as i32 * 31;
            if index == selected {
                rect(target, 5, y + 2, 3, 22, ink);
            }
            line(target, &clipped(&control.label, 40), 13, y, ink);
            rect(target, 13, y + 15, 237, 7, Rgb565::new(5, 10, 5));
            if control.minimum_percent < 0 {
                let center = 131;
                let width = u32::from(control.value_percent.unsigned_abs()) * 118 / 100;
                let x = if control.value_percent < 0 {
                    center - width as i32
                } else {
                    center
                };
                rect(target, x, y + 15, width, 7, ink);
                rect(target, center, y + 13, 1, 11, ink);
            } else {
                rect(
                    target,
                    13,
                    y + 15,
                    u32::from(control.value_percent.unsigned_abs()) * 237 / 100,
                    7,
                    ink,
                );
            }
            line(
                target,
                &format!("{:>3}%", control.value_percent),
                267,
                y + 12,
                ink,
            );
        }
    }
    if !connected {
        let message = if screen.phase == "error" {
            let code = screen.error_code.as_deref().unwrap_or("E203");
            format!(
                "{}  {}",
                code,
                if code == "E302" {
                    "Stop failed. Check device"
                } else {
                    "Reconnect failed"
                }
            )
        } else {
            format!(
                "Reconnecting... {}s  Controls disabled",
                screen.elapsed_seconds
            )
        };
        line(target, &message, 10, 193, Rgb565::WHITE);
        footer(
            target,
            "DISCONNECT",
            "",
            if screen.phase == "error" {
                "SEARCH AGAIN"
            } else {
                ""
            },
        );
    } else {
        let hint = screen
            .error_code
            .as_ref()
            .map(|code| format!("{code}  Command failed. Try STOP."))
            .unwrap_or_else(|| "Left knob: value   Right knob: setting".to_owned());
        line(target, &hint, 10, 193, Rgb565::WHITE);
        footer(target, "DISCONNECT", "STOP", "NEXT");
    }
}
pub fn render(screen: &RadBleScreenState) -> FrameBuffer {
    let mut target = FrameBuffer::new();
    header(&mut target, &screen.phase);
    match screen.phase.as_str() {
        "searching" => render_search(&mut target, screen),
        "menu" => {
            title(&mut target, "RADR");
            line(&mut target, "Find a device", 14, 90, Rgb565::WHITE);
            footer(&mut target, "", "", "SEARCH");
        }
        "connected" | "reconnecting" => render_controls(&mut target, screen),
        "error" if screen.connected_name.is_some() => render_controls(&mut target, screen),
        _ => render_wait(&mut target, screen),
    }
    target
}
