// The counter of the terminal comparison (F4; ../../README.md): a count and two buttons; the mouse clicks them, Tab
// moves between them, Enter presses, Ctrl+C or q ends. ratatui has no button: two bordered paragraphs and their
// hit test, the focused one reversed.
use crossterm::event::{self, DisableMouseCapture, EnableMouseCapture, Event, KeyCode, KeyModifiers, MouseButton, MouseEventKind};
use crossterm::execute;
use ratatui::layout::{Constraint, Layout, Position, Rect};
use ratatui::style::{Style, Stylize};
use ratatui::widgets::{Block, Paragraph};

fn main() -> std::io::Result<()> {
    let mut terminal = ratatui::init();
    execute!(std::io::stdout(), EnableMouseCapture)?;
    let mut count: i64 = 0;
    let mut focus: usize = 0;
    let mut areas = [Rect::default(); 2];
    loop {
        terminal.draw(|frame| {
            let [top, buttons] = Layout::vertical([Constraint::Length(2), Constraint::Length(3)]).margin(1).areas(frame.area());
            frame.render_widget(Paragraph::new(format!("Count {count}")), top);
            let [minus, plus] = Layout::horizontal([Constraint::Length(6), Constraint::Length(6)]).spacing(1).areas(buttons);
            areas = [minus, plus];
            for (index, (area, label)) in [(minus, "-1"), (plus, "+1")].into_iter().enumerate() {
                let style = if index == focus { Style::new().reversed() } else { Style::new() };
                frame.render_widget(Paragraph::new(label).centered().style(style).block(Block::bordered()), area);
            }
        })?;
        let press = |which: usize, count: &mut i64| if which == 0 { *count = (*count - 1).max(0) } else { *count += 1 };
        match event::read()? {
            Event::Key(key) if key.code == KeyCode::Char('q') || (key.code == KeyCode::Char('c') && key.modifiers.contains(KeyModifiers::CONTROL)) => break,
            Event::Key(key) if key.code == KeyCode::Tab => focus = (focus + 1) % 2,
            Event::Key(key) if key.code == KeyCode::Enter => press(focus, &mut count),
            Event::Mouse(mouse) if mouse.kind == MouseEventKind::Down(MouseButton::Left) => {
                for (index, area) in areas.iter().enumerate() {
                    if area.contains(Position::new(mouse.column, mouse.row)) {
                        focus = index;
                        press(index, &mut count);
                    }
                }
            }
            _ => {}
        }
    }
    execute!(std::io::stdout(), DisableMouseCapture)?;
    ratatui::restore();
    Ok(())
}
