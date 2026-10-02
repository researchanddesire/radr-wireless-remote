use crate::rad_ble::{self, RadBleScreenState, SharedRadBleState};
use crate::ui::{FrameBuffer, render};
use anyhow::Context;
use display_interface_spi::SPIInterfaceNoCS;
use embedded_graphics::{pixelcolor::Rgb565, prelude::*, primitives::Rectangle};
use embedded_hal::spi::MODE_3;
use esp_idf_svc::hal::{
    delay::Ets,
    gpio::{Gpio0, Gpio4, Gpio5, Gpio6, Gpio7, Gpio15, Gpio16, Output, PinDriver},
    spi::{SPI2, SpiDeviceDriver, SpiDriverConfig, config},
    units::FromValueType,
};
use mipidsi::{Builder, Orientation};
use std::fmt::Debug;

pub const WIDTH: u32 = 320;
pub const HEIGHT: u32 = 240;

pub trait RadrDisplay {
    fn refresh(&mut self, state: &SharedRadBleState) -> Result<bool, String>;
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
