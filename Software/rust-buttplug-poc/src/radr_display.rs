use crate::rad_ble::{self, RadBleScreenState, SharedRadBleState};
use anyhow::Context;
use display_interface_spi::SPIInterfaceNoCS;
use embedded_graphics::{
    mono_font::{
        MonoTextStyle,
        ascii::{FONT_6X10, FONT_10X20},
    },
    pixelcolor::Rgb565,
    prelude::*,
    primitives::{PrimitiveStyle, Rectangle},
    text::{Baseline, Text},
};
use embedded_hal::spi::MODE_3;
use esp_idf_svc::hal::{
    delay::Ets,
    gpio::{Gpio0, Gpio4, Gpio5, Gpio6, Gpio7, Gpio15, Gpio16, Output, PinDriver},
    spi::{SPI2, SpiDeviceDriver, SpiDriverConfig, config},
    units::FromValueType,
};
use mipidsi::{Builder, Orientation};
use std::{convert::Infallible, fmt::Debug};

pub const WIDTH: u32 = 320;
pub const HEIGHT: u32 = 240;

pub trait RadrDisplay {
    fn refresh(&mut self, state: &SharedRadBleState) -> Result<bool, String>;
}

struct FrameBuffer {
    pixels: Vec<Rgb565>,
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
    value.chars().take(maximum).collect()
}

fn draw_line(target: &mut FrameBuffer, value: &str, x: i32, y: i32, color: Rgb565) {
    let style = MonoTextStyle::new(&FONT_6X10, color);
    let _ = Text::with_baseline(value, Point::new(x, y), style, Baseline::Top).draw(target);
}

fn draw_header(target: &mut FrameBuffer, panel: Rgb565) {
    let _ = Rectangle::new(Point::zero(), Size::new(WIDTH, 32))
        .into_styled(PrimitiveStyle::with_fill(panel))
        .draw(target);
    let _ = Text::with_baseline(
        "RADR + UPSTREAM BUTTPLUG",
        Point::new(8, 6),
        MonoTextStyle::new(&FONT_10X20, Rgb565::WHITE),
        Baseline::Top,
    )
    .draw(target);
}

fn render_scan(screen: &RadBleScreenState) -> FrameBuffer {
    let mut target = FrameBuffer::new();
    let background = Rgb565::new(1, 3, 5);
    let panel = Rgb565::new(2, 8, 12);
    let cyan = Rgb565::new(3, 48, 29);
    let muted = Rgb565::new(15, 30, 16);
    let warning = Rgb565::new(31, 34, 2);

    target.pixels.fill(background);
    draw_header(&mut target, panel);
    draw_line(
        &mut target,
        &format!(
            "{} | OFFICIAL 9571b3db | BLE",
            screen.phase.to_ascii_uppercase()
        ),
        9,
        39,
        muted,
    );
    draw_line(
        &mut target,
        &format!(
            "FOUND  {} supported BLE candidate{}",
            screen.candidate_count,
            if screen.candidate_count == 1 { "" } else { "s" }
        ),
        9,
        53,
        Rgb565::WHITE,
    );

    if screen.candidates.is_empty() {
        draw_line(
            &mut target,
            "Scanning for known BLE services...",
            13,
            82,
            cyan,
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
            .take(6)
            .enumerate()
        {
            let marker = if index == selected { ">" } else { " " };
            let color = if index == selected {
                cyan
            } else {
                Rgb565::WHITE
            };
            draw_line(
                &mut target,
                &format!("{marker} {:02} {}", index + 1, clipped(&candidate.name, 43)),
                8,
                74 + row as i32 * 19,
                color,
            );
        }
    }

    if let Some(error) = screen.last_error.as_deref() {
        draw_line(
            &mut target,
            &format!("ERROR  {}", clipped(error, 44)),
            9,
            190,
            warning,
        );
    } else {
        let protocols = if screen.selected_protocols.is_empty() {
            "Selected protocol: -".to_owned()
        } else {
            format!(
                "Selected protocol: {}",
                screen.selected_protocols.join(", ")
            )
        };
        draw_line(&mut target, &clipped(&protocols, 50), 9, 190, muted);
    }

    let _ = Rectangle::new(Point::new(0, 208), Size::new(WIDTH, 32))
        .into_styled(PrimitiveStyle::with_fill(panel))
        .draw(&mut target);
    draw_line(&mut target, "< PREV", 10, 219, Rgb565::WHITE);
    draw_line(&mut target, "ACCEPT", 139, 219, cyan);
    draw_line(&mut target, "NEXT >", 268, 219, Rgb565::WHITE);
    target
}

fn render_connected(screen: &RadBleScreenState) -> FrameBuffer {
    let mut target = FrameBuffer::new();
    let background = Rgb565::new(1, 3, 5);
    let panel = Rgb565::new(2, 8, 12);
    let bar = Rgb565::new(7, 16, 17);
    let accent = Rgb565::new(3, 48, 29);
    let muted = Rgb565::new(15, 30, 16);
    let warning = Rgb565::new(31, 34, 2);

    target.pixels.fill(background);
    draw_header(&mut target, panel);
    draw_line(
        &mut target,
        &format!(
            "CONNECTED  {}",
            clipped(screen.connected_name.as_deref().unwrap_or("device"), 39)
        ),
        9,
        39,
        accent,
    );
    draw_line(
        &mut target,
        &format!(
            "{} feature(s) | {} modifiable setting(s)",
            screen.feature_count,
            screen.controls.len()
        ),
        9,
        52,
        muted,
    );

    if screen.controls.is_empty() {
        draw_line(
            &mut target,
            "No upstream output settings on this device",
            13,
            84,
            Rgb565::WHITE,
        );
    } else {
        let selected = screen
            .selected_control_index
            .unwrap_or(0)
            .min(screen.controls.len() - 1);
        let start = selected.saturating_sub(3);
        for (row, (index, control)) in screen
            .controls
            .iter()
            .enumerate()
            .skip(start)
            .take(4)
            .enumerate()
        {
            let y = 67 + row as i32 * 31;
            let selected_row = index == selected;
            if selected_row {
                let _ = Rectangle::new(Point::new(3, y), Size::new(3, 23))
                    .into_styled(PrimitiveStyle::with_fill(accent))
                    .draw(&mut target);
            }
            draw_line(
                &mut target,
                &format!(
                    "{} {}",
                    if selected_row { ">" } else { " " },
                    clipped(&control.label, 38)
                ),
                9,
                y,
                if selected_row { accent } else { Rgb565::WHITE },
            );
            let bar_y = y + 13;
            let _ = Rectangle::new(Point::new(14, bar_y), Size::new(222, 8))
                .into_styled(PrimitiveStyle::with_fill(bar))
                .draw(&mut target);
            let filled = u32::from(control.value_percent) * 222 / 100;
            if filled > 0 {
                let _ = Rectangle::new(Point::new(14, bar_y), Size::new(filled, 8))
                    .into_styled(PrimitiveStyle::with_fill(accent))
                    .draw(&mut target);
            }
            draw_line(
                &mut target,
                &format!("{:>3}%", control.value_percent),
                247,
                y + 11,
                if selected_row { accent } else { muted },
            );
        }
    }

    if let Some(error) = screen.last_error.as_deref() {
        draw_line(
            &mut target,
            &format!("ERROR  {}", clipped(error, 44)),
            9,
            193,
            warning,
        );
    } else {
        draw_line(
            &mut target,
            "LEFT KNOB: VALUE  |  RIGHT KNOB: SELECT",
            9,
            193,
            muted,
        );
    }

    let _ = Rectangle::new(Point::new(0, 208), Size::new(WIDTH, 32))
        .into_styled(PrimitiveStyle::with_fill(panel))
        .draw(&mut target);
    draw_line(&mut target, "< PREV", 10, 219, Rgb565::WHITE);
    draw_line(&mut target, "STOP", 145, 219, accent);
    draw_line(&mut target, "NEXT >", 268, 219, Rgb565::WHITE);
    target
}

fn render(screen: &RadBleScreenState) -> FrameBuffer {
    if screen.connected_name.is_some() {
        render_connected(screen)
    } else {
        render_scan(screen)
    }
}

fn publish_framebuffer(state: &SharedRadBleState, buffer: &FrameBuffer) {
    let storage = buffer
        .pixels
        .iter()
        .copied()
        .map(IntoStorage::into_storage)
        .collect::<Vec<u16>>();
    rad_ble::set_framebuffer(state, &storage);
}

struct HeadlessRenderer {
    last: Option<RadBleScreenState>,
}

impl RadrDisplay for HeadlessRenderer {
    fn refresh(&mut self, state: &SharedRadBleState) -> Result<bool, String> {
        let next = rad_ble::screen_state(state);
        if self.last.as_ref() == Some(&next) {
            return Ok(false);
        }
        let buffer = render(&next);
        publish_framebuffer(state, &buffer);
        self.last = Some(next);
        Ok(true)
    }
}

struct HardwareRenderer<D> {
    display: D,
    _backlight: PinDriver<'static, Output>,
    last: Option<RadBleScreenState>,
}

impl<D> RadrDisplay for HardwareRenderer<D>
where
    D: DrawTarget<Color = Rgb565>,
    D::Error: Debug,
{
    fn refresh(&mut self, state: &SharedRadBleState) -> Result<bool, String> {
        let next = rad_ble::screen_state(state);
        if self.last.as_ref() == Some(&next) {
            return Ok(false);
        }
        let buffer = render(&next);
        self.display
            .fill_contiguous(
                &Rectangle::new(Point::zero(), Size::new(WIDTH, HEIGHT)),
                buffer.pixels.iter().copied(),
            )
            .map_err(|error| format!("could not write ST7789 framebuffer: {error:?}"))?;
        publish_framebuffer(state, &buffer);
        self.last = Some(next);
        Ok(true)
    }
}

#[allow(clippy::too_many_arguments)]
pub fn initialize(
    spi: SPI2<'static>,
    sclk: Gpio5<'static>,
    mosi: Gpio6<'static>,
    chip_select: Gpio4<'static>,
    data_command: Gpio7<'static>,
    reset: Gpio16<'static>,
    backlight: Gpio15<'static>,
) -> Box<dyn RadrDisplay> {
    let result = (|| -> anyhow::Result<Box<dyn RadrDisplay>> {
        let reset = PinDriver::output(reset).context("could not configure ST7789 reset")?;
        let data_command =
            PinDriver::output(data_command).context("could not configure ST7789 data/command")?;
        let mut backlight =
            PinDriver::output(backlight).context("could not configure ST7789 backlight")?;
        let spi_config = config::Config::new()
            .baudrate(40.MHz().into())
            .data_mode(MODE_3);
        let device = SpiDeviceDriver::new_single(
            spi,
            sclk,
            mosi,
            None::<Gpio0<'static>>,
            Some(chip_select),
            &SpiDriverConfig::new(),
            &spi_config,
        )
        .context("could not initialize ST7789 SPI bus")?;
        let interface = SPIInterfaceNoCS::new(device, data_command);
        let mut delay = Ets;
        let display = Builder::st7789(interface)
            .with_display_size(240, 320)
            .with_orientation(Orientation::LandscapeInverted(true))
            .init(&mut delay, Some(reset))
            .map_err(|error| anyhow::anyhow!("could not initialize ST7789: {error:?}"))?;
        backlight
            .set_high()
            .context("could not turn on ST7789 backlight")?;
        Ok(Box::new(HardwareRenderer {
            display,
            _backlight: backlight,
            last: None,
        }))
    })();

    match result {
        Ok(display) => {
            log::info!("RADR ST7789 display initialized at {WIDTH}x{HEIGHT}");
            display
        }
        Err(error) => {
            log::error!("RADR display hardware unavailable; retaining BLE framebuffer: {error:#}");
            Box::new(HeadlessRenderer { last: None })
        }
    }
}
