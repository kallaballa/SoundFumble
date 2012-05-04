#include <gtk-2.0/gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include "aplay/aplay.hpp"

typedef struct
{
  gint samplerate;
  gint channel;
  gint format;
  gint off;
  gint lines;
  gint loop;
} PcmConfig;

static gchar* PCM_FORMATS[]={ "S8", "U8", "S16_LE", "S16_BE", "U16_LE", "U16_BE", "S24_LE", "S24_BE", "U24_LE", "U24_BE", "S32_LE", "S32_BE", "U32_LE", "U32_BE", "FLOAT_LE", "FLOAT_BE", "FLOAT64_LE", "FLOAT64_BE", "IEC958_SUBFRAME_LE", "IEC958_SUBFRAME_BE", "MU_LAW", "A_LAW", "IMA_ADPCM", "MPEG", "GSM", "SPECIAL", "S24_3LE", "S24_3BE", "U24_3LE", "U24_3BE", "S20_3LE", "S20_3BE", "U20_3LE", "U20_3BE", "S18_3LE", "S18_3BE", "U18_3LE" };
static gchar* PCM_RATES[]={ "4000", "8000", "11025", "16000", "22050", "32000", "44100", "48000" };

static void query (void);
static void run   (const gchar      *name,
                   gint              nparams,
                   const GimpParam  *param,
                   gint             *nreturn_vals,
                   GimpParam       **return_vals);
static void fumble  (GimpDrawable     *drawable);
static gboolean fumble_dialog (GimpDrawable *drawable);

static PcmConfig pcmconf =
{
  6,1,1,0,0,0
};

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,
  NULL,
  query,
  run
};

MAIN()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    {
      GIMP_PDB_INT32,
      "run-mode",
      "Run mode"
    },
    {
      GIMP_PDB_IMAGE,
      "image",
      "Input image"
    },
    {
      GIMP_PDB_DRAWABLE,
      "drawable",
      "Input drawable"
    }
  };

  gimp_install_procedure (
    "plug-in-soundfumble",
    "Sound Fumble",
    "Play the image as raw pcm",
    "Amir Hassan",
    "Copyright amir@viel-zu.org",
    "2011",
    "_Sound Fumble",
    "RGB*, GRAY*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("plug-in-soundfumble",
                             "<Image>/Filters/Sound");
}

off_t chunk_pos = 0;

void push_pcm(uint8_t* chunk, uint8_t d) {
  if(chunk_pos >= chunk_bytes) {
    chunk_pos = 0;
    pcm_write(chunk, chunk_size);
  }

  chunk[ chunk_pos++ ] = d;
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  fprintf(stderr, "%s\n", "run");
  static GimpParam  values[1];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  GimpRunMode       run_mode;
  GimpDrawable     *drawable;

  /* Setting mandatory output values */
  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /* Getting run_mode - we won't display a dialog if
   * we are in NONINTERACTIVE mode */
  run_mode = param[0].data.d_int32;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  switch (run_mode)
  {
    case GIMP_RUN_INTERACTIVE:
      /* Get options last values if needed */
      gimp_get_data ("plug-in-soundfumble", &pcmconf);
      fprintf(stderr, "%s\n", "switch");
      /* Display the dialog */
      if (! fumble_dialog (drawable))
        return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
/*      if (nparams != 4)
        status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
        bvals.radius = param[3].data.d_int32;*/
      break;

    case GIMP_RUN_WITH_LAST_VALS:
     /*  Get options last values if needed  */
     gimp_get_data ("plug-in-soundfumble", &pcmconf);
      break;

    default:
      break;
    }
  if (run_mode == GIMP_RUN_INTERACTIVE)
    gimp_set_data ("plug-in-soundfumble", &pcmconf, sizeof (PcmConfig));

  fumble (drawable);

  gimp_displays_flush ();
  gimp_drawable_detach (drawable);

  return;
}

static void
fumble (GimpDrawable *drawable)
{
  fprintf(stderr, "%s\n", "fumble");
  gint         i, j, k, channels;
  gint         x1, y1, x2, y2;
  GimpPixelRgn rgn_in;
  guchar      *row1;

  char s_rate[8];
  char s_channels[2];
  char s_format[256];

  sprintf(s_rate, "-r%s",  PCM_RATES[pcmconf.samplerate]);
  sprintf(s_channels, "-c%d",  pcmconf.channel);
  sprintf(s_format, "-f%s",  PCM_FORMATS[pcmconf.format]);

  fprintf(stderr, "samplerate: %s\n",  s_rate);
  fprintf(stderr, "channels: %s\n",  s_channels);
  fprintf(stderr, "format: %s\n",  s_format);

  char *argv[] = {"fumble", s_rate, s_channels, s_format};
  playback_init(4, argv);
  playback_open();

  gimp_drawable_mask_bounds (drawable->drawable_id,
                             &x1, &y1,
                             &x2, &y2);
  channels = gimp_drawable_bpp (drawable->drawable_id);

  gimp_pixel_rgn_init (&rgn_in,
                       drawable,
                       x1, y1,
                       x2 - x1, y2 - y1,
                       FALSE, FALSE);

  row1 = g_new (guchar, channels * (x2 - x1));
  uint8_t chunk[chunk_bytes];

  int loops=0;
  if (pcmconf.off > 0)
    y1 = pcmconf.off;

  if (pcmconf.lines > 0)
    y2 = y1 + pcmconf.lines;
;
  while(pcmconf.loop==-1 || (loops++ <= pcmconf.loop)) {
    gimp_progress_update(1);
    for (i = y1; i < y2; i++) {
      gimp_pixel_rgn_get_row(&rgn_in, row1, x1, MAX(y1, i - 1), x2 - x1);

      for (j = x1; j < x2; j++) {
        for (k = 0; k < channels; k++) {
          push_pcm(chunk, row1[channels * (j - x1) + k]);
        }
      }

      if (i % 10 == 0)
        gimp_progress_update((gdouble)(i - y1) / (gdouble)(y2 - y1));
    }
  }
  g_free (row1);

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id,
                        x1, y1,
                        x2 - x1, y2 - y1);
  playback_quit();
}

static gboolean
fumble_dialog (GimpDrawable *drawable)
{

  GtkWidget *dialog;
  GtkWidget *pcm_vbox;
  GtkWidget *pcm_hbox;
  GtkWidget *pcm_frame;
  GtkWidget *pcm_alignment;
  GtkWidget *play_vbox;
  GtkWidget *play_hbox;
  GtkWidget *play_frame;
  GtkWidget *play_alignment;
  GtkWidget *rate_label;
  GtkWidget *format_label;
  GtkWidget *channel_label;
  GtkWidget *off_spin_label;
  GtkWidget *line_spin_label;
  GtkWidget *loop_spin_label;

  GtkWidget *channel_spin;
  GtkObject *channel_spin_adj;
  GtkWidget *off_spin;
  GtkObject *off_spin_adj;
  GtkWidget *lines_spin;
  GtkObject *lines_spin_adj;
  GtkWidget *loop_spin;
  GtkObject *loop_spin_adj;
  GtkWidget *pcm_frame_label;
  GtkWidget *play_frame_label;
  GtkWidget *samplerate_box;
  GtkWidget *format_box;
  gboolean   run;

  gimp_ui_init ("SoundFumble", FALSE);

  gint         x1, y1, x2, y2;
  gimp_drawable_mask_bounds (drawable->drawable_id,
                             &x1, &y1,
                             &x2, &y2);

  dialog = gimp_dialog_new ("Sound Fumble", "SoundFumble",
                            NULL, 0,
                            gimp_standard_help_func, "plug-in-soundfumble",

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  pcm_vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), pcm_vbox);
  gtk_widget_show (pcm_vbox);

  pcm_frame = gtk_frame_new (NULL);
  gtk_widget_show (pcm_frame);
  gtk_box_pack_start (GTK_BOX (pcm_vbox), pcm_frame, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (pcm_frame), 6);

  pcm_alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (pcm_alignment);
  gtk_container_add (GTK_CONTAINER (pcm_frame), pcm_alignment);
  gtk_alignment_set_padding (GTK_ALIGNMENT (pcm_alignment), 6, 6, 6, 6);

  pcm_hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (pcm_hbox);
  gtk_container_add (GTK_CONTAINER (pcm_alignment), pcm_hbox);

  play_vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), play_vbox);
  gtk_widget_show (play_vbox);

  play_frame = gtk_frame_new (NULL);
  gtk_widget_show (play_frame);
  gtk_box_pack_start (GTK_BOX (play_vbox), play_frame, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (play_frame), 6);

  play_alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (play_alignment);
  gtk_container_add (GTK_CONTAINER (play_frame), play_alignment);
  gtk_alignment_set_padding (GTK_ALIGNMENT (play_alignment), 6, 6, 6, 6);

  play_hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (play_hbox);
  gtk_container_add (GTK_CONTAINER (play_alignment), play_hbox);

  rate_label = gtk_label_new_with_mnemonic ("_Rate:");
  gtk_widget_show (rate_label);
  gtk_box_pack_start (GTK_BOX (pcm_hbox), rate_label, FALSE, FALSE, 6);

  samplerate_box = gtk_combo_box_new_text ();
  int i = 0;
  for(; i < (sizeof(PCM_RATES)/sizeof(gchar*)); i++) {
    gtk_combo_box_append_text(samplerate_box, PCM_RATES[i]);
  }
  gtk_combo_box_set_active (samplerate_box, pcmconf.samplerate);
  gtk_widget_show (samplerate_box);
  gtk_box_pack_start (GTK_BOX (pcm_hbox), samplerate_box, FALSE, FALSE, 6);

  format_label = gtk_label_new_with_mnemonic ("_Format:");
  gtk_widget_show (format_label);
  gtk_box_pack_start (GTK_BOX (pcm_hbox), format_label, FALSE, FALSE, 6);

  format_box = gtk_combo_box_new_text ();
  i = 0;

  for (; i < (sizeof(PCM_FORMATS) / sizeof(gchar*)); i++) {
    gtk_combo_box_append_text(format_box, PCM_FORMATS[i]);
  }

  gtk_combo_box_set_active (format_box, pcmconf.format);
  gtk_widget_show (format_box);
  gtk_box_pack_start (GTK_BOX (pcm_hbox), format_box, FALSE, FALSE, 6);

  channel_label = gtk_label_new_with_mnemonic ("_Channels:");
  gtk_widget_show (channel_label);
  gtk_box_pack_start (GTK_BOX (pcm_hbox), channel_label, FALSE, FALSE, 6);

  channel_spin_adj = gtk_adjustment_new (1, 1, 32, 1, 1, 0);
  channel_spin = gtk_spin_button_new (GTK_ADJUSTMENT (channel_spin_adj), 1, 0);
  gtk_spin_button_set_value (channel_spin, pcmconf.channel);
  gtk_widget_show (channel_spin);
  gtk_box_pack_start (GTK_BOX (pcm_hbox), channel_spin, FALSE, FALSE, 6);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (channel_spin), TRUE);

  off_spin_label = gtk_label_new_with_mnemonic ("Lines _offset:");
  gtk_widget_show ( off_spin_label);
  gtk_box_pack_start (GTK_BOX (play_hbox), off_spin_label, FALSE, FALSE, 6);

  off_spin_adj = gtk_adjustment_new (1, 1, x2 - x1, 1, 1, 0);
  off_spin = gtk_spin_button_new (GTK_ADJUSTMENT (off_spin_adj), 1, 0);
  gtk_spin_button_set_value (off_spin, pcmconf.off);
  gtk_widget_show (off_spin);
  gtk_box_pack_start (GTK_BOX (play_hbox), off_spin, FALSE, FALSE, 6);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (off_spin), TRUE);

  line_spin_label = gtk_label_new_with_mnemonic ("Lines _range (0=end) :");
  gtk_widget_show ( line_spin_label);
  gtk_box_pack_start (GTK_BOX (play_hbox), line_spin_label, FALSE, FALSE, 6);

  lines_spin_adj = gtk_adjustment_new (1, 0, x2 - x1, 1, 1, 0);
  lines_spin = gtk_spin_button_new (GTK_ADJUSTMENT (lines_spin_adj), 1, 0);
  gtk_spin_button_set_value (lines_spin, pcmconf.lines);
  gtk_widget_show (lines_spin);
  gtk_box_pack_start (GTK_BOX (play_hbox), lines_spin, FALSE, FALSE, 6);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (lines_spin), TRUE);

  loop_spin_label = gtk_label_new_with_mnemonic ("_Loops (-1=infinit):");
  gtk_widget_show ( loop_spin_label);
  gtk_box_pack_start (GTK_BOX (play_hbox), loop_spin_label, FALSE, FALSE, 6);

  loop_spin_adj = gtk_adjustment_new (1, -1, x2 - x1, 1, 1, 0);
  loop_spin = gtk_spin_button_new (GTK_ADJUSTMENT (loop_spin_adj), 1, 0);
  gtk_spin_button_set_value (loop_spin, pcmconf.loop);
  gtk_widget_show (loop_spin);
  gtk_box_pack_start (GTK_BOX (play_hbox), loop_spin, FALSE, FALSE, 6);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (loop_spin), TRUE);

  pcm_frame_label = gtk_label_new ("<b>Pcm Settings</b>");
  gtk_widget_show (pcm_frame_label);
  gtk_frame_set_label_widget (GTK_FRAME (pcm_frame), pcm_frame_label);
  gtk_label_set_use_markup (GTK_LABEL (pcm_frame_label), TRUE);

  play_frame_label = gtk_label_new ("<b>Playback Settings</b>");
  gtk_widget_show (play_frame_label);
  gtk_frame_set_label_widget (GTK_FRAME (play_frame), play_frame_label);
  gtk_label_set_use_markup (GTK_LABEL (play_frame_label), TRUE);

  g_signal_connect (channel_spin_adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &pcmconf.channel);

  g_signal_connect (off_spin_adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &pcmconf.off);

  g_signal_connect (lines_spin_adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &pcmconf.lines);

  g_signal_connect (loop_spin_adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &pcmconf.loop);

  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  pcmconf.samplerate=gtk_combo_box_get_active (samplerate_box);
  pcmconf.format=gtk_combo_box_get_active (format_box);

  gtk_widget_destroy (dialog);

  return run;
}

