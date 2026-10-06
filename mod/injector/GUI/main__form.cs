using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Windows.Forms;

namespace Custom_UEVR_Injector
{
    public partial class main__form : Form
    {
        string getArgs = Functions.GetValidArguments();

        private Timer update_config_timer;

        string form_title = null;
        bool loop_check = false;

        public main__form()
        {
            InitializeComponent();
            button_game_folder.Enabled = !TargetPathBinding.Request.IsPathBound;
            button_shortcut.Enabled = !TargetPathBinding.Request.IsPathBound;
            Slides.Initialize(this);

			form_title = this.Text;
            listResults.Text = "";

            this.Shown += load_args_config;

            update_config_timer = new Timer();
            update_config_timer.Interval = 1000;
            update_config_timer.Tick += update_config_Tick;
            update_config_timer.Start();

        }

        private async void load_args_config(object sender, EventArgs e)
        {
            await Task.Delay(500);

            if (getArgs != null)
            {
                Functions.game_executable = getArgs;

                listResults.Text = "";

                listResults.AppendText("[LOG] Executable: " + Functions.game_executable + Environment.NewLine);
                if (TargetPathBinding.Request.IsPathBound)
                    listResults.AppendText("[LOG] Required game path: " + TargetPathBinding.Request.TargetPath + Environment.NewLine);
                button_game_folder.Text = Functions.game_executable;

                Functions.CreateCheckProfile(Functions.game_executable, this);
                Functions.LoadExistingProfileSettings(this);

                this.Text = $"[{Functions.game_executable}] " + form_title;

				loop_check = true;
            }
        }

        private void update_config_Tick(object sender, EventArgs e)
        {
            LogSyncManager.SyncLog(this.listResults);

            if (!Functions.ProfileExists()) return;

                Functions.SaveGraphicsConfigs();

                if (Functions.config_txt_update)
                {
                    string config_txt_new = null;

                    foreach (KeyValuePair<string, object> entry in Functions.config_txt_data)
                    {
                        string chave = entry.Key;
                        object valor = entry.Value;

                        config_txt_new += chave + "=" + valor + Environment.NewLine;
                    }

                    string config_txt_path = Path.Combine(Functions.profile_path, "config.txt");
                    if (File.Exists(config_txt_path) && !string.IsNullOrWhiteSpace(config_txt_new))
                    {
                        File.WriteAllText(config_txt_path, config_txt_new);
                    }

                    Functions.config_txt_update = false;
                }

                if (Functions.injector_config_update)
                {
                    string injector_config_new = null;

                    foreach (KeyValuePair<string, object> entry in Functions.injector_config_data)
                    {
                        string chave = entry.Key;
                        object valor = entry.Value;

                        injector_config_new += chave + "=" + valor + Environment.NewLine;
                    }

                    string injector_config_path = Path.Combine(Functions.profile_path, "injector_config.txt");
                    if (File.Exists(injector_config_path) && !string.IsNullOrWhiteSpace(injector_config_new))
                    {
                        File.WriteAllText(injector_config_path, injector_config_new);
                    }

                    Functions.injector_config_update = false;
                }

                button_shortcut.Enabled = !TargetPathBinding.Request.IsPathBound;
                if (!button_profile_folder.Enabled) { button_profile_folder.Enabled = true; }

            if (Functions.UEVRFolderExists())
            {

                InjectionManager.MonitorProcess(this);

                if (loop_check) {

                    if ($"{Functions.injector_config_data["custom_var_auto_inject"]}" == "1")
                    {
                        listResults.AppendText("[LOG] Auto Inject Enabled." + Environment.NewLine);

                        LoopManager.StartLoop(this, () =>
                        {
                            if (InjectionManager.CanAutoInject())
                            {
                                InjectionManager.InjectAll(this);
                            }
                        });

                    }
                    else
                    {
                        listResults.AppendText("[LOG] Auto Inject Desabled." + Environment.NewLine);

                        LoopManager.StopLoop();
                    }

                    loop_check = false;

                }

            }
            else
            {
                if (Functions.game_executable == null && Functions.uevr_folder == null)
                {
                    button_inject.Text = "Select the game folder and the UEVR folder";
                }
                else if (Functions.game_executable != null && Functions.uevr_folder == null)
                {
                    button_inject.Text = "Select UEVR folder now";
                }
                else if (Functions.game_executable == null && Functions.uevr_folder != null)
                {
                    button_inject.Text = "Select the game folder now";
                }

                button_inject.Enabled = false;
            }

        }

        private void button_game_folder_Click(object sender, EventArgs e)
        {
            if (TargetPathBinding.Request.IsPathBound)
            {
                listResults.AppendText("This launch is bound to the selected game path. Close this injector before choosing another game." + Environment.NewLine);
                return;
            }
            string selectedPath = FolderDialogHelper.ShowFolderDialog(this.Handle);

            if (!string.IsNullOrEmpty(selectedPath))
            {
                string game_exe = ExeFinder.game_exe_finder(selectedPath, this);

                if (game_exe != null)
                {
                    Functions.game_executable = game_exe;

					listResults.Text = "";

					listResults.AppendText("[LOG] Executable: " + Functions.game_executable + Environment.NewLine);
					button_game_folder.Text = Functions.game_executable;

					Functions.CreateCheckProfile(Functions.game_executable, this);
					Functions.LoadExistingProfileSettings(this);

					this.Text = $"[{Functions.game_executable}] " + form_title;

					loop_check = true;

                }
                else
                {
                    listResults.AppendText("ERROR: Game executable not found!" + Environment.NewLine);
                }
            } else {
				listResults.AppendText("ERROR: Folder dialog returned empty!" + Environment.NewLine);
			}

        }

        private void button_uevr_folder_Click(object sender, EventArgs e)
        {
            string selectedPath = FolderDialogHelper.ShowFolderDialog(this.Handle);

            if (!string.IsNullOrEmpty(selectedPath))
            {

                string uevr_dir = ExeFinder.uevr_folder_finder(selectedPath, this);

                if (uevr_dir != null)
                {
                    Functions.uevr_folder = uevr_dir;

                    listResults.AppendText("[LOG] UEVR Folder: " + Functions.uevr_folder + Environment.NewLine);
                    button_uevr_folder.Text = Functions.uevr_folder;
                    Functions.injector_config_data["custom_var_urvr_folder"] = Functions.uevr_folder;
    					loop_check = true;
                }
                else
                {
                    listResults.AppendText("ERROR: UEVR folder not found!" + Environment.NewLine);
                }
            } else {
				listResults.AppendText("ERROR: Folder dialog returned empty!" + Environment.NewLine);
			}
			
        }

        private void radioButtonXR_CheckedChanged(object sender, EventArgs e)
        {
            if (radioButtonXR.Checked)
            {
                Functions.injector_config_data["custom_var_runtime"] = "0";
                Functions.DLL_files.Remove("openvr_api.dll");
                Functions.AddDLL("openxr_loader.dll");
                Functions.Sort_DLLs(Functions.DLL_files);
            }
            Functions.injector_config_update = true;
        }

        private void radioButtonVR_CheckedChanged(object sender, EventArgs e)
        {
            if (radioButtonVR.Checked)
            {
                Functions.injector_config_data["custom_var_runtime"] = "1";
                Functions.DLL_files.Remove("openxr_loader.dll");
                Functions.AddDLL("openvr_api.dll");
                Functions.Sort_DLLs(Functions.DLL_files);
            }
            Functions.injector_config_update = true;
        }

        private void checkBoxNullify_CheckedChanged(object sender, EventArgs e)
        {
            if (checkBoxNullify.Checked)
            {
                Functions.injector_config_data["custom_var_nullify"] = "1";
                Functions.AddDLL("UEVRPluginNullifier.dll");
                Functions.Sort_DLLs(Functions.DLL_files);
            } else
            {
                Functions.injector_config_data["custom_var_nullify"] = "0";
                Functions.DLL_files.Remove("UEVRPluginNullifier.dll");
                Functions.Sort_DLLs(Functions.DLL_files);
            }
            Functions.injector_config_update = true;
        }

        private void checkBoxFPS_CheckedChanged(object sender, EventArgs e)
        {
            Functions.config_txt_data["VR_ShowFPSOverlay"] = checkBoxFPS.Checked ? "true" : "false";
            Functions.config_txt_update = true;
        }

        private void radioButtonNative_CheckedChanged(object sender, EventArgs e)
        {
            if (radioButtonNative.Checked) { Functions.config_txt_data["VR_RenderingMethod"] = "0"; }
            Functions.config_txt_update = true;
        }

        private void radioButtonSync_CheckedChanged(object sender, EventArgs e)
        {
            if (radioButtonSync.Checked) { Functions.config_txt_data["VR_RenderingMethod"] = "1"; }
            Functions.config_txt_update = true;
        }

        private void radioButtonAlt_CheckedChanged(object sender, EventArgs e)
        {
            if (radioButtonAlt.Checked) { Functions.config_txt_data["VR_RenderingMethod"] = "2"; }
            Functions.config_txt_update = true;
        }

        private void checkBoxAutoInject_CheckedChanged(object sender, EventArgs e)
        {
            Functions.injector_config_data["custom_var_auto_inject"] = checkBoxAutoInject.Checked ? "1" : "0";
            Functions.injector_config_update = true;
			loop_check = true;
        }

        private void checkBoxFocusGame_CheckedChanged(object sender, EventArgs e)
        {
            Functions.injector_config_data["custom_var_auto_focus"] = checkBoxFocusGame.Checked ? "1" : "0";
            Functions.injector_config_update = true;
        }

        private void checkBox_auto_close_CheckedChanged(object sender, EventArgs e)
        {
            Functions.injector_config_data["custom_var_auto_close"] = checkBox_auto_close.Checked ? "1" : "0";
            Functions.injector_config_update = true;
        }

        private void button_profile_folder_Click(object sender, EventArgs e)
        {
            if (Directory.Exists(Functions.profile_path))
            {
                var psi = new ProcessStartInfo
                {
                    FileName = Functions.profile_path,
                    UseShellExecute = true,
                    Verb = "open"
                };
                Process.Start(psi);
            }
        }

        private void button_shortcut_Click(object sender, EventArgs e)
        {
            if (TargetPathBinding.Request.IsPathBound)
            {
                listResults.AppendText("Use the launcher's saved Steam choice. A name-only shortcut would lose the required game path." + Environment.NewLine);
                return;
            }
            if (Functions.game_executable != null)
            {
                Shortcut.create_Shortcut(Functions.game_executable, this);
            }
        }

        private void button_inject_Click(object sender, EventArgs e)
        {
            InjectionManager.InjectAll(this);
        }

    }
}
