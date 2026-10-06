using System;
using System.Collections.Generic;
using System.Globalization;
using System.Drawing;
using System.Windows.Forms;

namespace Custom_UEVR_Injector
{
    public class SliderGroup
    {
        public SmoothSlider Slider { get; private set; }
        public Label TitleLabel { get; private set; }
        public Label DescriptionLabel { get; private set; }
        public Label ValueLabel { get; private set; }

        private bool loadingSavedValue;
        private readonly ToolTip graphicsHint = new ToolTip();

        private const int TitleSpacing = 60;
        private const int ValueSpacing = 10;
        private const int DescriptionSpacing = 40;
        private const int VerticalPadding = 5;

        public SliderGroup(string title, Point position, int width, float min, float max, float initialValue, bool asInteger = false, string description = "")
        {
            TitleLabel = new Label
            {
                Text = title,
                AutoSize = true
            };

            TitleLabel.BackColor = Color.Transparent;
            TitleLabel.Font = new Font(TitleLabel.Font.Name, 9);

            ValueLabel = new Label
            {
                Text = asInteger ? ((int)initialValue).ToString() : initialValue.ToString("F1"),
                AutoSize = true
            };

            ValueLabel.BackColor = Color.Transparent;
            ValueLabel.Font = new Font(TitleLabel.Font.Name, 9);

            DescriptionLabel = new Label
            {
                Text = description,
                AutoSize = true
            };

            DescriptionLabel.BackColor = Color.Transparent;

            Slider = new SmoothSlider
            {
                Width = width,
                Minimum = min,
                Maximum = max,
                Value = initialValue,
                SnapToInt = asInteger,
                Height = 40,
                Enabled = true,
                Cursor = Cursors.Hand

            }; //Enabled = false

            //Slider.BackColor = Color.Transparent;

            // Positioning
            TitleLabel.Location = new Point(
                position.X,
                position.Y + (Slider.Height / 2) - (TitleLabel.PreferredHeight / 2)
            );

            Slider.Location = new Point(
                TitleLabel.Right + TitleSpacing,
                position.Y + VerticalPadding
            );

            ValueLabel.Location = new Point(
                Slider.Right + ValueSpacing,
                position.Y + (Slider.Height / 2) - (ValueLabel.PreferredHeight / 2)
            );

            DescriptionLabel.Location = new Point(
                ValueLabel.Right - DescriptionSpacing,
                position.Y + (Slider.Height / 2) - (DescriptionLabel.PreferredHeight / 2)
            );

            Slider.ValueChanged += (sender, e) =>
            {
                if (loadingSavedValue) return;
                if (asInteger)
                    ValueLabel.Text = ((int)Slider.Value).ToString();
                else
                    ValueLabel.Text = Slider.Value.ToString("F1");

                if (Functions.ProfileExists())
                {
					// user_script
					string user_script_array_key = null;
					
                    if (TitleLabel.Text == "Resolution Quality:")
                        user_script_array_key = "sg.ResolutionQuality";
                    else if (TitleLabel.Text == "View Distance Quality:")
                        user_script_array_key = "sg.ViewDistanceQuality";
                    else if (TitleLabel.Text == "Anti-Aliasing Quality:")
                        user_script_array_key = "sg.AntiAliasingQuality";
                    else if (TitleLabel.Text == "Post Process Quality:")
                        user_script_array_key = "sg.PostProcessQuality";
                    else if (TitleLabel.Text == "Shadow Quality:")
                        user_script_array_key = "sg.ShadowQuality";
                    else if (TitleLabel.Text == "Texture Quality:")
                        user_script_array_key = "sg.TextureQuality";
                    else if (TitleLabel.Text == "Effects Quality:")
                        user_script_array_key = "sg.EffectsQuality";
                    else if (TitleLabel.Text == "Foliage Quality:")
                        user_script_array_key = "sg.FoliageQuality";
                    else if (TitleLabel.Text == "Shading Quality:")
                        user_script_array_key = "sg.ShadingQuality";
                    else if (TitleLabel.Text == "Reflection Quality:")
                        user_script_array_key = "sg.ReflectionQuality";
                    else if (TitleLabel.Text == "VSync:")
                        user_script_array_key = "r.VSync";
                    else if (TitleLabel.Text == "Volumetric Cloud:")
                        user_script_array_key = "r.VolumetricCloud";
                    else if (TitleLabel.Text == "Global Illumination Quality:")
                        user_script_array_key = "sg.GlobalIlluminationQuality";
                    else if (TitleLabel.Text == "Reflection Method:")
                        user_script_array_key = "r.ReflectionMethod";

					if (user_script_array_key != null)
					{
                        var value = Slider.Value.ToString(CultureInfo.InvariantCulture);
                        if (!PersistPolicyOverride("user_script.txt", user_script_array_key, value)) return;
                        Functions.user_script_data[user_script_array_key] = value;
                        Functions.user_script_update = Functions.graphics_profile == null;
					}
					
					// cvars_standard
					string cvars_standard_array_key = null;
					
                    if (TitleLabel.Text == "Screen Percentage:")
                        cvars_standard_array_key = "Core_r.ScreenPercentage";
					
					if (cvars_standard_array_key != null)
					{
                        var value = Slider.Value.ToString(CultureInfo.InvariantCulture);
                        if (!PersistPolicyOverride("cvars_standard.txt", cvars_standard_array_key, value)) return;
                        Functions.cvars_standard_data[cvars_standard_array_key] = value;
                        Functions.cvars_standard_update = Functions.graphics_profile == null;
					}
					
				}

            };
        }

        private bool PersistPolicyOverride(string file, string key, string value)
        {
            if (Functions.graphics_profile == null) return true;
            try
            {
                Functions.graphics_profile.SetOverride(file, key, value);
                graphicsHint.SetToolTip(Slider, "Saved startup override. Takes effect on the next injection; not a live reading.");
                return true;
            }
            catch (Exception ex)
            {
                ValueLabel.Text = "Error";
                graphicsHint.SetToolTip(Slider, "Override was not saved: " + ex.Message);
                return false;
            }
        }

        public void LoadSavedValue(IDictionary<string, object> values, string key)
        {
            loadingSavedValue = true;
            try
            {
                object raw;
                float number;
                if (!values.TryGetValue(key, out raw))
                {
                    ValueLabel.Text = "Game";
                    graphicsHint.SetToolTip(Slider, "No saved injector override. Current game value is unknown. Drag to save an explicit override for the next injection.");
                }
                else if (float.TryParse(Convert.ToString(raw), NumberStyles.Float, CultureInfo.InvariantCulture, out number)
                    && !float.IsNaN(number) && !float.IsInfinity(number))
                {
                    Slider.Value = number;
                    ValueLabel.Text = Convert.ToString(raw);
                    graphicsHint.SetToolTip(Slider, "Saved startup override, not a live game reading. Drag to change it for the next injection.");
                }
                else
                {
                    ValueLabel.Text = "Custom";
                    graphicsHint.SetToolTip(Slider, "Unrecognized saved value; preserved unchanged. Drag only to replace this override.");
                }
            }
            finally { loadingSavedValue = false; }
        }

        public void AddToForm(Control parent)
        {
            parent.Controls.Add(TitleLabel);
            parent.Controls.Add(Slider);
            parent.Controls.Add(ValueLabel);
            parent.Controls.Add(DescriptionLabel);
        }
    }
}
