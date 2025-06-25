// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gst/gst.h>

#define SEEK_TEST_COUNT 5

static void
print_usage (const char *program_name)
{
  g_print ("Usage: %s <file_name>\n", program_name);
}

int
main (int argc, char *argv[])
{
  gchar *play_uri = NULL;
  GstElement *playbin = NULL;
  GstMessage *msg = NULL;
  gint64 duration = 0, random_time = 0, position = 0;

  if (argc < 2) {
    print_usage (argv[0]);
    return -1;
  }

  gst_init (&argc, &argv);

  play_uri = gst_uri_is_valid (argv[1]) ? g_strdup (argv[1]) :
      gst_filename_to_uri (argv[1], NULL);
  if (!play_uri) {
    g_printerr ("Failed to create URI from file name!\n");
    return -1;
  }
  g_print ("The playbin URI is %s\n", play_uri);

  playbin = gst_element_factory_make ("playbin", NULL);
  if (!playbin) {
    g_printerr ("Failed to create playbin element!\n");
    g_free (play_uri);
    return -1;
  }
  g_object_set (playbin, "uri", play_uri, NULL);
  g_free (play_uri);

  if (gst_element_set_state (playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to set playbin to PAUSED state!\n");
    gst_object_unref (playbin);
    return -1;
  }

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (playbin),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR);
  if (!msg) {
    g_printerr ("Failed to preroll!\n");
    gst_element_set_state (playbin, GST_STATE_NULL);
    gst_object_unref (playbin);
    return -1;
  }
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    g_printerr ("Received error message!\n");
    gst_message_unref (msg);
    gst_element_set_state (playbin, GST_STATE_NULL);
    gst_object_unref (playbin);
    return -1;
  }
  gst_message_unref (msg);

  if (!gst_element_query_duration (playbin, GST_FORMAT_TIME, &duration)) {
    g_printerr ("Failed to query duration!\n");
    gst_element_set_state (playbin, GST_STATE_NULL);
    gst_object_unref (playbin);
    return -1;
  }
  g_print ("The duration is: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (duration));

  srand ((unsigned int)time (NULL));
  for (gint32 seek_test = 0; seek_test < SEEK_TEST_COUNT; seek_test++) {
    g_print ("Playback...\n");
    gst_element_set_state (playbin, GST_STATE_PLAYING);
    g_usleep (3 * G_USEC_PER_SEC);

    random_time = (gint64) (rand () % (duration / GST_SECOND)) * GST_SECOND;
    g_print ("Seek to: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (random_time));
    if (!gst_element_seek_simple (playbin, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH, random_time)) {
      g_printerr ("Failed to seek to %" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (random_time));
      break;
    }

    g_print ("Playback...\n");
    g_usleep (5 * G_USEC_PER_SEC);

    if (gst_element_set_state (playbin, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
      g_printerr ("Failed to pause!\n");
      break;
    }

    if (!gst_element_query_position (playbin, GST_FORMAT_TIME, &position)) {
      g_printerr ("Failed to query position!\n");
      break;
    }
    g_print ("Pause at: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (position));
    g_usleep (3 * G_USEC_PER_SEC);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);

  return 0;
}
