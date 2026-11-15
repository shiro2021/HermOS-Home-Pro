//! HermOS AI agent — Phase 2 bridge (ping-pong test).
//!
//! Runs as a Genode **libc** component.  Rust on Genode has no native bindings
//! for Genode sessions, so all I/O goes through the libc/VFS:
//!
//!   * the `ui_intent` HID report is exposed as a ROM by `report_rom` and
//!     mounted into the VFS at `/ui/ui_intent` (a `<rom>` node), so it is
//!     read here with an ordinary `std::fs` call;
//!   * the reply (`ai_command`) currently goes to the log (stdout -> /dev/log)
//!     because this tree has no `<report>` VFS plugin.  A small C++ `<report>`
//!     VFS plugin (or a C++ shim) is the planned fix so the agent can write
//!     its response back to `desktop_builder` through `report_rom`.

use std::fs;
use std::thread;
use std::time::Duration;

/// Pull the user prompt out of the report XML.  The exact wire format is still
/// being defined on the `desktop_builder` side (Phase 1d task 3), so this is
/// deliberately tolerant: it looks for a `prompt="..."` attribute and otherwise
/// falls back to the raw trimmed text.
fn extract_prompt(xml: &str) -> String {
    if let Some(start) = xml.find("prompt=\"") {
        let rest = &xml[start + 8..];
        if let Some(end) = rest.find('"') {
            return rest[..end].to_string();
        }
    }
    xml.trim().to_string()
}

fn main() {
    eprintln!("HermOS hermes_agent: waiting for ui_intent...");

    let mut last = String::new();

    loop {
        match fs::read_to_string("/ui/ui_intent") {
            Ok(xml) => {
                let xml = xml.trim().to_string();
                if !xml.is_empty() && xml != last {
                    last = xml.clone();
                    let prompt = extract_prompt(&xml);
                    let reply  = format!("Hermes AI received: [{}]", prompt);

                    /* echo to the log for serial/visual verification */
                    eprintln!("{}", reply);

                    /* write the reply into the ai_command report (round-trip) */
                    let _ = fs::write("/ai/ai_command", reply);
                }
            }
            Err(_) => { /* ROM not published yet */ }
        }

        thread::sleep(Duration::from_millis(200));
    }
}
