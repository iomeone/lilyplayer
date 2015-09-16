#include <signal.h>
#include <iostream>
#include <QFileDialog>
#include <QMessageBox>

#include "mainwindow.hh"
#include "ui_mainwindow.hh"

// Global variables to "share" state between the signal handler and
// the main event loop.  Only these two pieces should be allowed to
// use these global variables.  To avoid any other piece piece of code
// from using it, the declaration is not written on a header file on
// purpose.
extern volatile sig_atomic_t pause_requested;
extern volatile sig_atomic_t continue_requested;
extern volatile sig_atomic_t exit_requested;


void MainWindow::look_for_signals_change()
{

  if (exit_requested)
  {
    close();
  }

  if (pause_requested)
  {
    pause_requested = 0;
    is_in_pause = true;
  }

  if (continue_requested)
  {
    continue_requested = 0;
    is_in_pause = false;
  }

}

void MainWindow::process_keyboard_event(const music_event& keys_event)
{
  update_keyboard(keys_event, this->keyboard);
  draw_keyboard(*(this->scene), this->keyboard);

  for (const auto& message : keys_event.midi_messages)
  {
    auto tmp = message; // can't use message directly since message is const and
			// sendMessage doesn't take a const vector
    sound_player.sendMessage(&tmp);

    // could use the following to cast the const away: but since there is no
    // guarantee that the libRtMidi doesn't modify the data ... (I know the I
    // can read the code)
    //
    //sound_player.sendMessage(const_cast<midi_message*>(&message));
  }
}

void MainWindow::song_event_loop()
{
  if (song_pos == INVALID_SONG_POS)
  {
    return;
  }

  if (is_in_pause)
  {
    QTimer::singleShot(100, this, SLOT(song_event_loop()));
    return;
  }

  if ((song_pos != 0) and (song_pos >= song.size()))
  {
    throw std::runtime_error("Invalid song position found");
  }

  process_keyboard_event( song[song_pos] );

  if (song_pos + 1 == song.size())
  {
    // song is finished
    song_pos = INVALID_SONG_POS;
  }
  else
  {
    // call this function back for the next event
    song_pos++;
    QTimer::singleShot( static_cast<int>((song[song_pos].time - song[song_pos - 1].time) / 1'000'000), this, SLOT(song_event_loop()) );
  }

}

void MainWindow::stop_song()
{
  // for each key, create a release key event. No matter if the key was actually
  // pressed or not
  music_event all_keys_up;
  constexpr auto nb_keys = note_kind::do_8 - note_kind::la_0 + 1;
  all_keys_up.key_events.reserve(nb_keys);
  all_keys_up.midi_messages.reserve(nb_keys);

  for (auto key = static_cast<uint8_t>(note_kind::la_0);
       key <= static_cast<uint8_t>(note_kind::do_8);
       ++key)
  {
    all_keys_up.key_events.push_back(key_data{ .pitch = key,
					        .ev_type = key_data::type::released });
    all_keys_up.midi_messages.push_back(
      std::vector<uint8_t>{ 0x80, key, 0x00 } );
  }

  process_keyboard_event(all_keys_up);
}

void MainWindow::open_file()
{
  QStringList filters;
  filters << "Midi files (*.midi *.mid)"
  	  << "Any files (*)";

  QFileDialog dialog;
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setViewMode(QFileDialog::List);
  dialog.setNameFilters(filters);

  const auto dialog_ret = dialog.exec();
  if (dialog_ret == QDialog::Accepted)
  {
    const auto files = dialog.selectedFiles();
    if (files.length() == 1)
    {
      const auto file = files[0];
      const auto filename = file.toStdString();

      try
      {
	stop_song();
	const auto midi_events = get_midi_events(filename);
	const auto keyboard_events = get_key_events(midi_events);
	this->song = group_events_by_time(midi_events, keyboard_events);
	this->song_pos = 0;
	song_event_loop();
      }
      catch (std::exception& e)
      {
	const auto err_msg = e.what();
	QMessageBox::critical(this, tr("Failed to open file."),
			      err_msg,
			      QMessageBox::Ok,
			      QMessageBox::Ok);
      }

    }
  }
}

#pragma GCC diagnostic push
#if !defined(__clang__)
  #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant" // Qt is not effective-C++ friendy
#endif

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  scene(new QGraphicsScene(this)),
  keyboard(),
  signal_checker_timer(),
  song(),
  sound_player(RtMidi::LINUX_ALSA)
{
  ui->setupUi(this);
  ui->keyboard->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
  ui->keyboard->setScene(scene);
  draw_keyboard(*scene, this->keyboard);

  connect(&signal_checker_timer, SIGNAL(timeout()), this, SLOT(look_for_signals_change()));
  signal_checker_timer.start(100 /* ms */);

  sound_player.openPort(1);
}

#pragma GCC diagnostic pop

MainWindow::~MainWindow()
{
  delete ui;
  sound_player.closePort();
}
