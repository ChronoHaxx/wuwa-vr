using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32;

namespace WuWaVR.Manager
{
    public sealed class MainWindow : Window
    {
        PackageStore store;
        readonly LauncherBridge bridge;
        readonly RepoClient repo = new RepoClient();
        readonly LauncherUpdateService launcherUpdates;
        readonly CancellationTokenSource updateLifetime = new CancellationTokenSource();
        readonly Strings text = new Strings();
        readonly bool preview;
        readonly DispatcherTimer poll = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        Catalog catalog = RepoClient.BundledCatalog();
        CancellationTokenSource operation;
        TaskCompletionSource<bool> operationFinished;
        bool polling, closing, rendering, preserveActionFeedback, submittedJob;
        bool connectionReady, previewConnected;
        string connectionProblem = "";
        string lastJobSnapshot, lastNativeError = "";
        bool allowClose, closeChecking, unresolvedStartup;
        Func<string, bool> closeOnlyConfirmOverride;
        TimeSpan closeTimeout = TimeSpan.FromSeconds(6);
        ComboBox languages, releases, installed, source, fps, size, purpose, recordMic, recordMicVolume;
        // The microphone choices' Windows IDs ("" off, "default"), matching recordMic's items; the list as last shown.
        readonly List<string> micIds = new List<string> { "", "default" };
        string micList = "";
        static readonly int[] MicVolumes = { 50, 75, 100, 150, 200, 300 };
        ContentControl pageHost;
        ScrollViewer setupPage, troubleshootingPage;
        bool troubleshootingVisible, riskRestored;
        Expander advancedPanel, recoveryPanel, feedbackPanel, recordingOptions, controllerPanel;
        ControllerDiagnostics.Report controllerReport;
        CancellationTokenSource controllerOperation;
        bool controllerChecking;
        string controllerFailureKey, controllerNoticeKey;
        DateTime controllerCheckedAt;
        TextBlock controllerState;
        TextBlock launcherVersion;
        Button launcherUpdateButton;
        Button packageUpdateButton, packageRollbackButton;
        // Releases the startup update already tried this session; a failure is not retried in a loop.
        readonly HashSet<string> autoPackageTried = new HashSet<string>();
        // Updates offered at Launch in VR, by version: asked once per session.
        readonly HashSet<string> launchOffers = new HashSet<string>();
        TextBox controllerDetails;
        ProgressBar controllerProgress;
        Func<CancellationToken, Task<ControllerDiagnostics.Report>> controllerProbeOverride;
        Action<string> controllerCopyOverride;
        TimeSpan controllerTimeout = TimeSpan.FromSeconds(10);
        TimeSpan cancelGrace = TimeSpan.FromSeconds(4);
        Expander processRecoveryPanel;
        StackPanel processRecoveryRows;
        TextBlock processRecoveryState;
        RecoveryReport processRecoveryReport;
        bool processRecoveryFresh;
        string processRecoveryNotice = "processRecoveryNotScanned";
        readonly HashSet<int> processRecoverySelected = new HashSet<int>();
        Func<CancellationToken, Task<RecoveryReport>> processRecoveryScanOverride;
        Func<RecoveryReport, IEnumerable<int>, CancellationToken, Task<RecoveryReport>> processRecoveryStopOverride;
        Func<string, bool> processRecoveryConfirmOverride;
        Func<string, bool> uninstallConfirmOverride;
        Func<string, string, CancellationToken, Task<UninstallResult>> uninstallPrepareOverride;
        Expander uninstallPanel;
        TextBox uninstallResultText;
        string uninstallDetails = "";
        bool uninstallNeedsRecovery;
        FrameworkElement versionConsent;
        Border connectionPanel;
        TextBlock connectionMessage;
        CheckBox compatible, risk, recordSound, recordSoundFiles;
        TextBlock gamePath, gameHint, runtime, runtimeHint, packageState, launchState, catalogState, operationText, gameVersion, recordingState, riskNotice;
        GameDiscovery.Result discovery;
        StackPanel gameLocationOptions;
        ComboBox gameLocations;
        Func<CancellationToken, Task<GameDiscovery.Result>> gameChoicesOverride;
        Button runtimeCheck;
        TextBox details;
        ProgressBar progress;
        Button launchButton, cancelButton, recoveryShortcut;
        string catalogKey = "catalogBundled";
        Dictionary<string, object> status = new Dictionary<string, object>();
        readonly List<Button> actions = new List<Button>();
        readonly Brush background = Color("#090D1C"), card = Color("#111830"), foreground = Color("#F5F3EE"), muted = Color("#B5BDCC"), accent = Color("#D8B565");
        readonly Brush line = Color("#26304F"), activeLine = Color("#8A7238"), badge = Color("#2A2414");
        // Failures are red with a "!" (never colour alone): a row that is broken, the launch step
        // that failed, the hero's outline and the footer line of an action that failed.
        readonly Brush danger = Color("#E2735F"), dangerLine = Color("#7A3A2E");
        // Status rows show where the player is: done (check), current (outlined), still to come,
        // or a problem (red "!" and the reason under the row).
        enum StepState { Pending, Current, Done, Problem }
        readonly Border[] stepCards = new Border[3], stepBadges = new Border[3];
        readonly TextBlock[] stepNumbers = new TextBlock[3], rowProblems = new TextBlock[3];
        // The last action that failed, by the row it belongs to (-1: the launch, in the hero),
        // until the next action starts.
        string pendingActionKey, previewHint; int failedRow = -2; string failedMessage = "";
        Border heroCard; Button heroHelp;
        Border launcherUpdateBar;
        TextBlock launcherUpdateBarText;
        // The one-screen layout (3 Nov 2026): a hero with the launch, three status rows, and the
        // fixes on the troubleshooting page with everything else under More options.
        TextBlock heroTitle;
        readonly TextBlock[] phaseChips = new TextBlock[4];
        StackPanel phaseRow, runtimeOptions;
        Border consentNotice;
        Button playFlatButton;
        Expander morePanel;
        // When the current launch phase was first seen, for "taking longer than usual".
        string shownPhase = "";
        DateTime phaseSince = DateTime.UtcNow;
        static readonly TimeSpan SlowVrStart = TimeSpan.FromSeconds(90);
        public MainWindow(PackageStore store, string backendData, bool preview = false, string language = null,
            LauncherUpdateService launcherUpdates = null)
        {
            this.store = store; this.preview = preview; bridge = new LauncherBridge(backendData);
            this.launcherUpdates = preview ? new LauncherUpdateService() : launcherUpdates ?? new LauncherUpdateService();
            text.Language = Strings.Supported(language ?? store.State.language);
            Title = "WuWa VR"; Width = 780; Height = 720; MinWidth = 640; MinHeight = 580;
            Background = background; Foreground = foreground; FontFamily = new FontFamily("Segoe UI"); FontSize = 14;
            Resources.Add(typeof(Button), ButtonStyle());
            Resources.Add(typeof(ComboBox), ChoiceStyle());
            Resources.Add(typeof(CheckBox), CheckStyle());
            Resources.Add(typeof(Expander), ExpanderStyle());
            Resources.Add(typeof(System.Windows.Controls.Primitives.ScrollBar), ScrollStyle());
            UseLayoutRounding = true;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Render();
            poll.Tick += async (s, e) => await RefreshStatus();
            Loaded += async (s, e) =>
            {
                if (preview) return;
                await Run(async c =>
                {
                    await DiscoverGame(c);
                    if (store.Selected != null)
                    {
                        // Discovery is informational; an attached job owns startup feedback.
                        operationText.Text = text["busy"];
                        await Connect(c);
                    }
                });
                // Opening is not an operation the player asked for: no "Operation complete." line.
                if (operationText.Text == text["actionComplete"]) operationText.Text = "";
                poll.Start();
                await Task.WhenAll(CheckLauncherUpdates(updateLifetime.Token, true), UpdateCatalog(updateLifetime.Token, true));
                await AutoUpdatePackage();
            };
            Closing += async (s, e) =>
            {
                if (!allowClose && !preview)
                {
                    e.Cancel = true;
                    if (closeChecking) return;
                    closeChecking = true;
                    try { if (await PrepareToClose()) { allowClose = true; Dispatcher.BeginInvoke(new Action(Close)); } }
                    finally { closeChecking = false; }
                    return;
                }
                closing = true; controllerOperation?.Cancel(); updateLifetime.Cancel(); this.launcherUpdates.Dispose();
                poll.Stop(); repo.Dispose(); bridge.Dispose();
            };
        }
        async Task<bool> PrepareToClose()
        {
            if (operation != null)
            {
                // Let local downloads/requests cancel cooperatively. Never tear down
                // a package write that has not yet returned to its safe boundary.
                var pending = operationFinished?.Task; operation.Cancel();
                if (pending != null) await Task.WhenAny(pending, Task.Delay(closeTimeout));
                if (operation != null)
                {
                    operationText.Text = text["closeBusy"]; details.Text = operationText.Text;
                    feedbackPanel.Visibility = Visibility.Visible; feedbackPanel.IsExpanded = true;
                    return false;
                }
            }
            // Closing never starts a replacement helper and never kills by process
            // name. A helper already identified as another package is not ours to stop.
            if (bridge.Conflict != null || store.Selected == null) return true;
            // Closing once VR is up is the normal end of a launch, but the worker still finishes
            // its check for up to 30 s. Hide the window now and stop the helper after it.
            bool hidden = false;
            if (bridge.Address != null && LauncherPresentation.InVr(status))
            {
                Hide(); hidden = true;
                try
                {
                    using (var limit = new CancellationTokenSource(TimeSpan.FromSeconds(90)))
                        while (LauncherPresentation.InVr(status = await bridge.Status(limit.Token))) await Task.Delay(2000, limit.Token);
                }
                catch (Exception) { return true; } // As "close only": anything still running keeps running.
            }
            bool stopped = false, cancelled = false; string remaining = "";
            await RunAction(async c => {
                using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(c))
                {
                    deadline.CancelAfter(closeTimeout);
                    try
                    {
                        // A sticky UI flag is not proof that an old worker is still
                        // alive. Re-read before deciding; never start a helper here.
                        if (bridge.Address != null)
                        {
                            status = await bridge.Status(deadline.Token);
                            var job = Json.Child(status, "job");
                            unresolvedStartup = LauncherBridge.LaunchWorkerRunning(status) ||
                                (Json.Flag(job, "running") && Json.Text(job, "kind") == "launch");
                            if (unresolvedStartup || Json.Flag(job, "running") || Json.Flag(Json.Child(status, "recording"), "running"))
                            {
                                remaining = text[unresolvedStartup ? "closeStartup" : "closeBusy"] + Environment.NewLine + StartupDetails(Json.Child(status, "launch"));
                                operationText.Text = remaining; return;
                            }
                        }
                        await bridge.CloseHelper(store.Folder(store.Selected), deadline.Token);
                        Disconnected(); unresolvedStartup = false; stopped = true;
                    }
                    catch (OperationCanceledException) when (c.IsCancellationRequested) { cancelled = true; throw; }
                    catch (Exception error)
                    {
                        remaining = text["closeHelperBlocked"] + Environment.NewLine +
                            (error is OperationCanceledException ? text["closeCheckTimeout"] : error.Message);
                        throw new InvalidOperationException(remaining, error);
                    }
                }
            }, false);
            if (stopped || cancelled) return stopped || hidden;
            if (hidden) return true; // Out of sight already: never ask from a hidden window.
            details.Text = remaining; feedbackPanel.Visibility = Visibility.Visible; feedbackPanel.IsExpanded = true;
            OpenProcessRecovery();
            string prompt = text["closeOnlyPrompt"] + Environment.NewLine + Environment.NewLine + remaining;
            return closeOnlyConfirmOverride != null ? closeOnlyConfirmOverride(prompt) :
                MessageBox.Show(this, prompt, text["closeOnlyTitle"], MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) == MessageBoxResult.Yes;
        }
        void OpenProcessRecovery()
        {
            ShowPage(true); morePanel.IsExpanded = true; processRecoveryPanel.IsExpanded = true;
            Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() => { if (!closing) processRecoveryPanel.BringIntoView(); }));
        }
        async Task CancelOrRecover()
        {
            if (operation != null) { operation.Cancel(); return; }
            if (bridge.Address == null || !LauncherBridge.CanCancelLaunch(status)) { OpenProcessRecovery(); return; }
            bool accepted = false;
            await RunAction(async c => {
                await bridge.Post("/api/cancel", new { }, c); accepted = true;
                operationText.Text = text["cancelRequested"];
                // The worker polls every three seconds. Give normal cooperative
                // cancellation time to finish before suggesting stuck-process recovery.
                using (var grace = CancellationTokenSource.CreateLinkedTokenSource(c))
                {
                    grace.CancelAfter(cancelGrace);
                    try {
                        while (true) {
                            status = await bridge.Status(grace.Token);
                            var job = Json.Child(status, "job");
                            if (!LauncherBridge.LaunchWorkerRunning(status) &&
                                !(Json.Flag(job, "running") && Json.Text(job, "kind") == "launch")) break;
                            await Task.Delay(250, grace.Token);
                        }
                    } catch (OperationCanceledException) when (!c.IsCancellationRequested && grace.IsCancellationRequested) { }
                }
            }, false);
            // An HTTP acknowledgement only requests cancellation. It does not
            // prove an old elevated worker released its lock or observed the request.
            if (!accepted || unresolvedStartup || !connectionReady)
            {
                if (accepted)
                {
                    operationText.Text = text["cancelPending"];
                    details.Text = operationText.Text + Environment.NewLine + StartupDetails(Json.Child(status, "launch"));
                    feedbackPanel.Visibility = Visibility.Visible; feedbackPanel.IsExpanded = true;
                }
                OpenProcessRecovery();
            }
            else { lastJobSnapshot = null; ShowStatus(); }
        }
        string StartupDetails(Dictionary<string, object> launch)
        {
            string message = Json.Text(launch, "message"); int pid;
            string owner = Json.Text(launch, "ownerPid");
            if (String.IsNullOrEmpty(owner)) owner = Json.Text(launch, "pid"); // Legacy receipts use pid with creation-time correlation.
            if (LauncherBridge.CurrentLaunch(status) && int.TryParse(owner, out pid) && pid > 0)
                message += Environment.NewLine + String.Format(text["startupProcess"], pid);
            return message;
        }
        static SolidColorBrush Color(string hex) { return new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex)); }
        Style ButtonStyle()
        {
            var style = new Style(typeof(Button));
            style.Setters.Add(new Setter(Control.BackgroundProperty, Color("#1B2444")));
            style.Setters.Add(new Setter(Control.ForegroundProperty, foreground));
            style.Setters.Add(new Setter(Control.BorderBrushProperty, Brushes.Transparent));
            style.Setters.Add(new Setter(Control.BorderThicknessProperty, new Thickness(1)));
            var template = new ControlTemplate(typeof(Button));
            var border = new FrameworkElementFactory(typeof(Border));
            border.SetValue(Border.CornerRadiusProperty, new CornerRadius(6));
            foreach (var binding in new[] {
                new { Property = Border.BackgroundProperty, Name = "Background" },
                new { Property = Border.BorderBrushProperty, Name = "BorderBrush" },
                new { Property = Border.BorderThicknessProperty, Name = "BorderThickness" },
                new { Property = Border.PaddingProperty, Name = "Padding" } })
                border.SetBinding(binding.Property, new System.Windows.Data.Binding(binding.Name) { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
            var content = new FrameworkElementFactory(typeof(ContentPresenter));
            content.SetValue(FrameworkElement.HorizontalAlignmentProperty, HorizontalAlignment.Center);
            content.SetValue(FrameworkElement.VerticalAlignmentProperty, VerticalAlignment.Center);
            content.SetValue(ContentPresenter.RecognizesAccessKeyProperty, true);
            border.AppendChild(content); template.VisualTree = border;
            style.Setters.Add(new Setter(Control.TemplateProperty, template));
            var hover = new Trigger { Property = IsMouseOverProperty, Value = true };
            hover.Setters.Add(new Setter(OpacityProperty, 0.88)); style.Triggers.Add(hover);
            var pressed = new Trigger { Property = Button.IsPressedProperty, Value = true };
            pressed.Setters.Add(new Setter(OpacityProperty, 0.75)); style.Triggers.Add(pressed);
            var focused = new Trigger { Property = IsKeyboardFocusedProperty, Value = true };
            focused.Setters.Add(new Setter(Control.BorderBrushProperty, accent));
            focused.Setters.Add(new Setter(Control.BorderThicknessProperty, new Thickness(2))); style.Triggers.Add(focused);
            // Disabled stays legible: the tooltip and status line explain why.
            var disabled = new Trigger { Property = IsEnabledProperty, Value = false };
            disabled.Setters.Add(new Setter(OpacityProperty, 0.5));
            disabled.Setters.Add(new Setter(CursorProperty, System.Windows.Input.Cursors.Arrow)); style.Triggers.Add(disabled);
            return style;
        }
        TextBlock Label(string value, double fontSize = 14, Brush brush = null)
        { return new TextBlock { Text = value, FontSize = fontSize, Foreground = brush ?? foreground, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 4) }; }
        Button Action(string key, Func<CancellationToken, Task> fn, bool primary = false)
        {
            var button = new Button { Content = text[key], Padding = new Thickness(14, 9, 14, 9), Margin = new Thickness(0, 4, 8, 4),
                Background = primary ? accent : Color("#1B2444"), Foreground = primary ? background : foreground, Cursor = System.Windows.Input.Cursors.Hand, Tag = key };
            ToolTipService.SetShowOnDisabled(button, true);
            button.Click += async (s, e) => { pendingActionKey = key; await Run(async c =>
            {
                await fn(c);
                if (!submittedJob && operationText.Text == text["busy"])
                    operationText.Text = text[key] + " · " + text["actionComplete"];
            }); }; actions.Add(button); return button;
        }
        // Which status row an action's failure belongs to; -1 is the launch itself (the hero).
        static int RowForAction(string key)
        {
            switch (key)
            {
                case "browse": case "useGameLocation": case "browseGameFile": case "scanGame": return 0;
                case "install": case "installVersion": case "updatePackage": case "rollbackPackage": case "rollback":
                case "repair": case "useVersion": case "reset": case "restore": case "retryConnection": case "switchLauncher": return 1;
                case "useHeadset": case "useSimulator": case "check": return 2;
                case "playFlat": return 0;
                case "launch": return -1;
                default: return -2;
            }
        }
        // A failed helper job, by its kind (wuwa_player.py begin_job).
        static int RowForJob(string kind)
        {
            switch (kind)
            {
                case "apply": case "verify": case "restore": return 1;
                case "runtime": case "check": return 2;
                case "launch": return -1;
                default: return -2;
            }
        }
        Button Link(string key, string url)
        { return Action(key, c => { Open(url); operationText.Text = text["browserOpened"]; return Task.CompletedTask; }); }
        // The website is the one place player docs live (site/, built in ten languages); the
        // launcher ships no guide of its own, so nothing here can fall out of date with it.
        const string Website = "https://chronohaxx.github.io/wuwa-vr/";
        string WebsiteUrl() { return Website + (text.Language == "en" ? "" : "l/" + text.Language + ".html"); }
        // A page that is English only (the problem-report form), saying so in other languages.
        Button GuideLink(string key, string page)
        {
            return Action(key, c =>
            {
                Open(Website + page);
                operationText.Text = text[text.Language != "en" ? "englishGuideFallback" : "browserOpened"];
                return Task.CompletedTask;
            });
        }
        Action<string> openOverride = null;
        void Open(string target)
        { if (!preview) { if (openOverride != null) openOverride(target); else Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); } }
        StackPanel Row(params UIElement[] elements)
        { var row = new StackPanel { Orientation = Orientation.Horizontal }; foreach (var e in elements) row.Children.Add(e); return row; }
        WrapPanel Wrap(params UIElement[] elements)
        { var row = new WrapPanel(); foreach (var e in elements) row.Children.Add(e); return row; }
        Border Card(string title, params UIElement[] elements)
        {
            var panel = new StackPanel(); var heading = Label(title, 16); heading.FontWeight = FontWeights.SemiBold; panel.Children.Add(heading);
            foreach (var e in elements) panel.Children.Add(e);
            return new Border { Background = card, BorderBrush = line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(12),
                Padding = new Thickness(18, 12, 18, 14), Margin = new Thickness(0, 0, 0, 10), Child = panel };
        }
        Style ChoiceStyle()
        {
            // Keep the ComboBox's native keyboard/selection behavior while styling its popup too.
            return (Style)System.Windows.Markup.XamlReader.Parse(@"
<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='{x:Type ComboBox}'>
 <Setter Property='Foreground' Value='#F5F3EE'/><Setter Property='Background' Value='#18203D'/><Setter Property='BorderBrush' Value='#344070'/>
 <Setter Property='ScrollViewer.HorizontalScrollBarVisibility' Value='Disabled'/>
 <Setter Property='ItemContainerStyle'><Setter.Value><Style TargetType='{x:Type ComboBoxItem}'>
  <Setter Property='Foreground' Value='#F5F3EE'/><Setter Property='Background' Value='#111830'/><Setter Property='Padding' Value='10,8'/>
  <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type ComboBoxItem}'><Border Background='{TemplateBinding Background}' Padding='{TemplateBinding Padding}'><ContentPresenter/></Border></ControlTemplate></Setter.Value></Setter>
  <Style.Triggers><Trigger Property='IsHighlighted' Value='True'><Setter Property='Background' Value='#3A3220'/></Trigger><Trigger Property='IsSelected' Value='True'><Setter Property='Background' Value='#4A3E22'/></Trigger></Style.Triggers>
 </Style></Setter.Value></Setter>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type ComboBox}'>
  <Grid>
   <Border x:Name='Shell' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='1' CornerRadius='6'/>
   <ToggleButton Focusable='False' IsChecked='{Binding IsDropDownOpen, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}' Background='Transparent'>
    <ToggleButton.Template><ControlTemplate TargetType='{x:Type ToggleButton}'><Border Background='{TemplateBinding Background}'/></ControlTemplate></ToggleButton.Template>
   </ToggleButton>
   <ContentPresenter Margin='11,8,30,8' IsHitTestVisible='False' VerticalAlignment='Center' Content='{TemplateBinding SelectionBoxItem}' ContentTemplate='{TemplateBinding SelectionBoxItemTemplate}' ContentTemplateSelector='{TemplateBinding ItemTemplateSelector}'/>
   <TextBlock Text='&#xE70D;' FontFamily='Segoe MDL2 Assets' FontSize='10' Margin='0,0,12,0' Foreground='#B5BDCC' HorizontalAlignment='Right' VerticalAlignment='Center' IsHitTestVisible='False'/>
   <Popup x:Name='PART_Popup' Placement='Bottom' IsOpen='{TemplateBinding IsDropDownOpen}' AllowsTransparency='True' Focusable='False' PopupAnimation='Fade'>
    <Border Background='#111830' BorderBrush='#344070' BorderThickness='1' CornerRadius='6' MinWidth='{Binding ActualWidth, RelativeSource={RelativeSource TemplatedParent}}' MaxHeight='280' Padding='3' Margin='0,3,0,0'>
     <ScrollViewer CanContentScroll='True' HorizontalScrollBarVisibility='Disabled'><ItemsPresenter KeyboardNavigation.DirectionalNavigation='Contained'/></ScrollViewer>
    </Border>
   </Popup>
  </Grid>
  <ControlTemplate.Triggers><Trigger Property='IsKeyboardFocusWithin' Value='True'><Setter TargetName='Shell' Property='BorderBrush' Value='#D8B565'/></Trigger><Trigger Property='IsMouseOver' Value='True'><Setter TargetName='Shell' Property='BorderBrush' Value='#D8B565'/></Trigger><Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.45'/></Trigger></ControlTemplate.Triggers>
 </ControlTemplate></Setter.Value></Setter>
</Style>");
        }
        // Dark-theme checkbox and expander; the stock ones draw white boxes and circles.
        static Style CheckStyle()
        {
            return (Style)System.Windows.Markup.XamlReader.Parse(@"
<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='{x:Type CheckBox}'>
 <Setter Property='Foreground' Value='#F5F3EE'/><Setter Property='Cursor' Value='Hand'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type CheckBox}'>
  <Grid Background='Transparent'><Grid.ColumnDefinitions><ColumnDefinition Width='Auto'/><ColumnDefinition/></Grid.ColumnDefinitions>
   <Border x:Name='Box' Width='18' Height='18' CornerRadius='4' Background='#18203D' BorderBrush='#5A6690' BorderThickness='1' VerticalAlignment='Top' Margin='0,4,10,0'>
    <TextBlock x:Name='Tick' Text='&#xE73E;' FontFamily='Segoe MDL2 Assets' FontSize='12' Foreground='#090D1C' HorizontalAlignment='Center' VerticalAlignment='Center' Visibility='Collapsed'/>
   </Border>
   <ContentPresenter Grid.Column='1' VerticalAlignment='Top' RecognizesAccessKey='True'/>
  </Grid>
  <ControlTemplate.Triggers>
   <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='Box' Property='BorderBrush' Value='#D8B565'/></Trigger>
   <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='Box' Property='BorderBrush' Value='#D8B565'/><Setter TargetName='Box' Property='BorderThickness' Value='2'/></Trigger>
   <Trigger Property='IsChecked' Value='True'><Setter TargetName='Box' Property='Background' Value='#D8B565'/><Setter TargetName='Box' Property='BorderBrush' Value='#D8B565'/><Setter TargetName='Tick' Property='Visibility' Value='Visible'/></Trigger>
   <Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.5'/></Trigger>
  </ControlTemplate.Triggers>
 </ControlTemplate></Setter.Value></Setter>
</Style>");
        }
        static Style ScrollStyle()
        {
            // A thin vertical thumb; horizontal bars keep the stock template.
            return (Style)System.Windows.Markup.XamlReader.Parse(@"
<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='{x:Type ScrollBar}'>
 <Style.Triggers><Trigger Property='Orientation' Value='Vertical'>
  <Setter Property='Width' Value='10'/><Setter Property='MinWidth' Value='10'/>
  <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type ScrollBar}'>
   <Track x:Name='PART_Track' IsDirectionReversed='True'>
    <Track.DecreaseRepeatButton><RepeatButton Command='ScrollBar.PageUpCommand' Opacity='0' Focusable='False'/></Track.DecreaseRepeatButton>
    <Track.IncreaseRepeatButton><RepeatButton Command='ScrollBar.PageDownCommand' Opacity='0' Focusable='False'/></Track.IncreaseRepeatButton>
    <Track.Thumb><Thumb><Thumb.Template><ControlTemplate TargetType='{x:Type Thumb}'>
     <Border x:Name='T' Background='#2C3760' CornerRadius='3' Margin='3,2,3,2'/>
     <ControlTemplate.Triggers><Trigger Property='IsMouseOver' Value='True'><Setter TargetName='T' Property='Background' Value='#46548A'/></Trigger>
      <Trigger Property='IsDragging' Value='True'><Setter TargetName='T' Property='Background' Value='#D8B565'/></Trigger></ControlTemplate.Triggers>
    </ControlTemplate></Thumb.Template></Thumb></Track.Thumb>
   </Track>
  </ControlTemplate></Setter.Value></Setter>
 </Trigger></Style.Triggers>
</Style>");
        }
        static Style ExpanderStyle()
        {
            return (Style)System.Windows.Markup.XamlReader.Parse(@"
<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='{x:Type Expander}'>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type Expander}'>
  <StackPanel>
   <ToggleButton x:Name='Header' Content='{TemplateBinding Header}' Foreground='{TemplateBinding Foreground}' HorizontalAlignment='Left' Cursor='Hand'
     IsChecked='{Binding IsExpanded, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}'>
    <ToggleButton.Template><ControlTemplate TargetType='{x:Type ToggleButton}'>
     <Border x:Name='Hit' Background='Transparent' Padding='0,5,10,5' BorderBrush='Transparent' BorderThickness='0,0,0,1'>
      <StackPanel Orientation='Horizontal'>
       <TextBlock x:Name='Chevron' Text='&#xE76C;' FontFamily='Segoe MDL2 Assets' FontSize='10' VerticalAlignment='Center' Margin='0,1,9,0' Foreground='{TemplateBinding Foreground}'/>
       <ContentPresenter VerticalAlignment='Center' TextElement.Foreground='{TemplateBinding Foreground}'/>
      </StackPanel>
     </Border>
     <ControlTemplate.Triggers>
      <Trigger Property='IsChecked' Value='True'><Setter TargetName='Chevron' Property='Text' Value='&#xE70D;'/></Trigger>
      <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='Chevron' Property='Foreground' Value='#D8B565'/></Trigger>
      <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='Hit' Property='BorderBrush' Value='#D8B565'/></Trigger>
     </ControlTemplate.Triggers>
    </ControlTemplate></ToggleButton.Template>
   </ToggleButton>
   <ContentPresenter x:Name='Body' Visibility='Collapsed' Margin='0,2,0,0'/>
  </StackPanel>
  <ControlTemplate.Triggers><Trigger Property='IsExpanded' Value='True'><Setter TargetName='Body' Property='Visibility' Value='Visible'/></Trigger></ControlTemplate.Triggers>
 </ControlTemplate></Setter.Value></Setter>
</Style>");
        }
        ComboBox Choice(params string[] items)
        {
            var control = new ComboBox { Margin = new Thickness(0, 4, 0, 4), MinWidth = 130, MaxDropDownHeight = 280 };
            foreach (string item in items) control.Items.Add(item); control.SelectedIndex = 0; return control;
        }
        // One line per setup part (game, VR mod, headset): a check when done, the title, what was
        // found and its hint, and the row's one action. Extra content (choices) folds out below.
        Border StatusRow(int index, string title, Button action, params UIElement[] elements)
        {
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(34) });
            // Titles share one width across the rows (the rows' panel is the size scope).
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto, MinWidth = 96, SharedSizeGroup = "rowTitle" });
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var numberText = Label((index + 1).ToString(), 11, muted); numberText.FontWeight = FontWeights.SemiBold; numberText.Margin = new Thickness(0);
            numberText.HorizontalAlignment = HorizontalAlignment.Center; numberText.VerticalAlignment = VerticalAlignment.Center;
            var stepBadge = new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius(11), Background = badge, BorderThickness = new Thickness(1), BorderBrush = Brushes.Transparent,
                Child = numberText, VerticalAlignment = VerticalAlignment.Top, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 4, 0, 0) };
            grid.Children.Add(stepBadge);
            var heading = Label(title, 13, muted); heading.Margin = new Thickness(0, 6, 16, 0); heading.VerticalAlignment = VerticalAlignment.Top;
            heading.TextWrapping = TextWrapping.NoWrap;
            Grid.SetColumn(heading, 1); grid.Children.Add(heading);
            var body = new StackPanel { Margin = new Thickness(0, 2, 12, 0) };
            foreach (var element in elements) body.Children.Add(element);
            var problem = Label("", 12, danger); problem.Margin = new Thickness(0, 4, 0, 2); problem.Visibility = Visibility.Collapsed;
            body.Children.Add(problem); rowProblems[index] = problem;
            Grid.SetColumn(body, 2); grid.Children.Add(body);
            if (action != null)
            {
                action.Margin = new Thickness(0, 2, 0, 0); action.Padding = new Thickness(14, 6, 14, 6); action.VerticalAlignment = VerticalAlignment.Top;
                Grid.SetColumn(action, 3); grid.Children.Add(action);
            }
            var result = new Border { BorderBrush = Brushes.Transparent, BorderThickness = new Thickness(0), Padding = new Thickness(0, 9, 0, 9), Child = grid };
            stepCards[index] = result; stepBadges[index] = stepBadge; stepNumbers[index] = numberText;
            return result;
        }
        void ShowStep(int index, StepState state, string problem = null)
        {
            if (stepCards[index] == null) return;
            bool bad = state == StepState.Problem;
            stepBadges[index].Background = state == StepState.Done ? accent : bad ? danger : badge;
            stepBadges[index].BorderBrush = state == StepState.Current ? accent : Brushes.Transparent;
            stepNumbers[index].Text = state == StepState.Done ? "✓" : bad ? "!" : (index + 1).ToString();
            stepNumbers[index].Foreground = state == StepState.Done || bad ? background : state == StepState.Current ? accent : muted;
            System.Windows.Automation.AutomationProperties.SetHelpText(stepBadges[index], problem ?? "");
            rowProblems[index].Text = problem ?? "";
            rowProblems[index].Visibility = String.IsNullOrEmpty(problem) ? Visibility.Collapsed : Visibility.Visible;
        }
        Border Divider() { return new Border { Height = 1, Background = line }; }
        static readonly string[] PhaseChipKeys = { "stepPermission", "stepPlay", "stepStartVr", "stepInVr" };
        UIElement PhaseChips()
        {
            // The four steps of a launch, shown while one runs (the current one is lit) and after
            // one fails or is cancelled (the step it stopped at is red "!" or marked "–").
            phaseRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 10, 0, 0), Visibility = Visibility.Collapsed };
            var keys = PhaseChipKeys;
            for (int i = 0; i < keys.Length; ++i)
            {
                phaseChips[i] = new TextBlock { Text = text[keys[i]], FontSize = 12, Foreground = muted, VerticalAlignment = VerticalAlignment.Center };
                phaseRow.Children.Add(new Border { Background = badge, CornerRadius = new CornerRadius(10), Padding = new Thickness(10, 3, 10, 4),
                    Margin = new Thickness(0, 0, 6, 0), Child = phaseChips[i] });
            }
            return phaseRow;
        }
        Button FooterLink(string key, string url)
        {
            var link = Link(key, url); link.Background = Brushes.Transparent; link.Foreground = muted; link.FontSize = 11;
            link.Padding = new Thickness(4, 3, 4, 3); link.Margin = new Thickness(0, 0, 12, 0); return link;
        }
        void Render()
        {
            // Changing language must translate the current screen, not reset the
            // selected package, recording options, consent or diagnostic evidence.
            bool rebuilding = pageHost != null;
            double scrollOffset = rebuilding && pageHost.Content is ScrollViewer ? ((ScrollViewer)pageHost.Content).VerticalOffset : 0;
            string releaseId = (releases?.SelectedItem as Release)?.id;
            string installedFolder = (installed?.SelectedItem as Installed)?.folder;
            int captureIndex = source?.SelectedIndex ?? 0, fpsIndex = fps?.SelectedIndex ?? 0,
                sizeIndex = size?.SelectedIndex ?? 1, purposeIndex = purpose?.SelectedIndex ?? 0;
            bool versionConfirmed = compatible?.IsChecked == true, riskConfirmed = risk?.IsChecked == true,
                advancedOpen = advancedPanel?.IsExpanded == true;
            bool recoveryOpen = recoveryPanel?.IsExpanded == true,
                feedbackOpen = feedbackPanel?.IsExpanded == true, recordingOptionsOpen = recordingOptions?.IsExpanded == true,
                controllerOpen = controllerPanel?.IsExpanded == true, processRecoveryOpen = processRecoveryPanel?.IsExpanded == true,
                uninstallOpen = uninstallPanel?.IsExpanded == true, moreOpen = morePanel?.IsExpanded == true,
                runtimeChoicesOpen = runtimeOptions?.Visibility == Visibility.Visible;
            string savedDetails = details?.Text, savedFeedback = operationText?.Text;
            rendering = true; actions.Clear();
            FlowDirection = Strings.RightToLeft(text.Language) ? FlowDirection.RightToLeft : FlowDirection.LeftToRight;
            var outer = new DockPanel { Margin = new Thickness(20, 14, 20, 10) };
            var header = new Grid { Margin = new Thickness(0, 0, 0, 12) };
            System.Windows.Input.KeyboardNavigation.SetTabIndex(header, 0);
            header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); header.ColumnDefinitions.Add(new ColumnDefinition());
            header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var logo = Mark(38); logo.VerticalAlignment = VerticalAlignment.Center; header.Children.Add(logo);
            var heading = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 12, 0) };
            var title = new TextBlock { Text = text["title"], FontSize = 20, FontWeight = FontWeights.SemiBold, Foreground = foreground, VerticalAlignment = VerticalAlignment.Center };
            var pill = new Border { Background = badge, CornerRadius = new CornerRadius(10), Padding = new Thickness(9, 2, 9, 3), Margin = new Thickness(10, 2, 0, 0), VerticalAlignment = VerticalAlignment.Center,
                Child = new TextBlock { Text = text["beta"], FontSize = 10, FontWeight = FontWeights.SemiBold, Foreground = accent } };
            heading.Children.Add(Row(title, pill));
            Grid.SetColumn(heading, 1); header.Children.Add(heading);
            languages = Choice(Strings.Names); languages.SelectedIndex = Math.Max(0, Array.IndexOf(Strings.Codes, text.Language));
            languages.MinWidth = 210; System.Windows.Automation.AutomationProperties.SetName(languages, text["language"]); languages.ToolTip = text["language"];
            languages.SelectionChanged += (s, e) =>
            {
                if (rendering || operation != null) return;
                text.Language = Strings.Codes[Math.Max(0, languages.SelectedIndex)];
                if (!preview) { store.State.language = text.Language; store.Save(); }
                Render();
            };
            var globe = new TextBlock { Text = "", FontFamily = new FontFamily("Segoe MDL2 Assets"), FontSize = 15, Foreground = muted,
                VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 8, 0) };
            var languagePanel = Row(globe, languages); languagePanel.VerticalAlignment = VerticalAlignment.Center;
            Grid.SetColumn(languagePanel, 2); header.Children.Add(languagePanel);
            DockPanel.SetDock(header, Dock.Top); outer.Children.Add(header);
            // The app's own update is separate from the VR package: a slim bar
            // appears only when there is something to do.
            launcherVersion = Label("", 11, muted); launcherVersion.Margin = new Thickness(0, 3, 0, 0); launcherVersion.TextWrapping = TextWrapping.NoWrap;
            launcherUpdateButton = Action("updateLauncher", async c => await UpdateLauncher(c), true);
            launcherUpdateButton.Margin = new Thickness(0); launcherUpdateButton.Padding = new Thickness(14, 6, 14, 6);
            launcherUpdateBarText = Label("", 13, foreground); launcherUpdateBarText.VerticalAlignment = VerticalAlignment.Center;
            var updateRow = new DockPanel(); DockPanel.SetDock(launcherUpdateButton, Dock.Right); updateRow.Children.Add(launcherUpdateButton); updateRow.Children.Add(launcherUpdateBarText);
            launcherUpdateBar = new Border { Background = Color("#241E10"), BorderBrush = activeLine, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10),
                Padding = new Thickness(14, 7, 8, 7), Margin = new Thickness(0, 0, 0, 12), Child = updateRow, Visibility = Visibility.Collapsed };
            DockPanel.SetDock(launcherUpdateBar, Dock.Top); outer.Children.Add(launcherUpdateBar);
            var footer = new StackPanel { Margin = new Thickness(0, 10, 0, 0) };
            System.Windows.Input.KeyboardNavigation.SetTabIndex(footer, 2);
            launchButton = Action("launch", async c =>
            {
                // First-run installation is the primary action. Never start the game
                // as a side effect of installing or selecting a runtime.
                if (WantsInstall()) { await Install(c); return; }
                await EnsureConnected(c);
                status = await bridge.Status(c);
                var runtimeBlock = LauncherPresentation.RuntimeLaunchBlock(connectionReady, status);
                if (runtimeBlock != null) throw new InvalidOperationException(text[runtimeBlock]);
                if (SteamChoiceNeedsPackage()) { OfferSteamPackageUpdate(); ShowStatus(); return; }
                if (discovery != null && discovery.Message == "gameMultiple" &&
                    String.IsNullOrEmpty(store.State.launcherPath) && !Json.Flag(Json.Child(status, "game"), "saved"))
                    throw new InvalidOperationException(text["gameMultiple"]);
                if (risk.IsChecked != true) throw new InvalidOperationException(text["riskAccept"]);
                if (!await OfferUpdateBeforeLaunch(c)) return;
                await bridge.Post("/api/settings", new { riskAcknowledged = true }, c);
                await SubmitJob("/api/launch", new { id = CurrentBuild() }, c);
            }, true);
            // The launch lives in the hero card (Setup); the footer keeps what both pages share:
            // progress, the operation line with Cancel, details and the links.
            launchState = Label(text["gameNotRunning"], 13, muted); launchState.Margin = new Thickness(0, 2, 0, 0);
            launchButton.MinWidth = 200; launchButton.FontWeight = FontWeights.SemiBold; launchButton.FontSize = 15;
            launchButton.Padding = new Thickness(26, 12, 26, 12); launchButton.Margin = new Thickness(0);
            playFlatButton = Action("playFlat", async c => await PlayWithoutVr(c));
            playFlatButton.Padding = new Thickness(18, 12, 18, 12); playFlatButton.Margin = new Thickness(0, 0, 10, 0);
            cancelButton = new Button { Content = text["cancel"], Padding = new Thickness(14, 6, 14, 6), Margin = new Thickness(10, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
            cancelButton.Click += async (s, e) => await CancelOrRecover();
            operationText = Label(preview ? text["preview"] : text["ready"], 12, muted); operationText.VerticalAlignment = VerticalAlignment.Center;
            progress = new ProgressBar { Minimum = 0, Maximum = 1, Height = 3, Foreground = accent, Background = card, Margin = new Thickness(0, 0, 0, 4) };
            var statusStrip = new DockPanel(); DockPanel.SetDock(cancelButton, Dock.Right); statusStrip.Children.Add(cancelButton); statusStrip.Children.Add(operationText);
            footer.Children.Add(progress); footer.Children.Add(statusStrip);
            recoveryShortcut = new Button { Content = text["processRecoveryTitle"], Tag = "processRecoveryOpen", HorizontalAlignment = HorizontalAlignment.Left };
            recoveryShortcut.Click += (s, e) => OpenProcessRecovery(); StyleFooter(recoveryShortcut); actions.Add(recoveryShortcut); footer.Children.Add(recoveryShortcut);
            details = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 65, MaxHeight = 110,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Background = card, Foreground = foreground, BorderThickness = new Thickness(0), Padding = new Thickness(10) };
            feedbackPanel = new Expander { Header = text["details"], Content = details, Foreground = muted, Visibility = Visibility.Collapsed };
            footer.Children.Add(feedbackPanel);
            footer.Children.Add(new Border { Height = 1, Background = line, Margin = new Thickness(0, 8, 0, 0) });
            // Guide and controls live on the website (in the player's language), not in the app.
            var website = Action("website", c => { Open(WebsiteUrl()); operationText.Text = text["browserOpened"]; return Task.CompletedTask; });
            StyleFooter(website);
            var footnotes = Wrap(PageLink("troubleshooting", true), website, FooterLink("support", "https://ko-fi.com/chronohax"));
            var footerRow = new DockPanel { Margin = new Thickness(0, 6, 0, 0) };
            DockPanel.SetDock(launcherVersion, Dock.Right); footerRow.Children.Add(launcherVersion); footerRow.Children.Add(footnotes);
            footer.Children.Add(footerRow); DockPanel.SetDock(footer, Dock.Bottom); outer.Children.Add(footer);
            pageHost = new ContentControl { HorizontalContentAlignment = HorizontalAlignment.Stretch, VerticalContentAlignment = VerticalAlignment.Stretch };
            System.Windows.Input.KeyboardNavigation.SetTabIndex(pageHost, 1);
            setupPage = Page(Setup());
            // Preserve the existing recording action implementation for isolated
            // regression coverage. The public surface opens full web tools instead.
            Recording();
            troubleshootingPage = Page(Help());
            pageHost.Content = troubleshootingVisible ? troubleshootingPage : setupPage;
            outer.Children.Add(pageHost);
            if (rebuilding)
            {
                var choice = releases.Items.Cast<Release>().FirstOrDefault(r => r.id == releaseId);
                if (choice != null) releases.SelectedItem = choice;
                compatible.IsChecked = choice != null && versionConfirmed; risk.IsChecked = riskConfirmed;
                installed.SelectedItem = store.State.installed.FirstOrDefault(i => i.folder == installedFolder) ?? store.Selected;
                source.SelectedIndex = captureIndex; fps.SelectedIndex = fpsIndex; size.SelectedIndex = sizeIndex; purpose.SelectedIndex = purposeIndex;
                advancedPanel.IsExpanded = advancedOpen;
                recoveryPanel.IsExpanded = recoveryOpen; recordingOptions.IsExpanded = recordingOptionsOpen;
                controllerPanel.IsExpanded = controllerOpen;
                processRecoveryPanel.IsExpanded = processRecoveryOpen;
                uninstallPanel.IsExpanded = uninstallOpen; morePanel.IsExpanded = moreOpen;
                runtimeOptions.Visibility = runtimeChoicesOpen ? Visibility.Visible : Visibility.Collapsed;
                details.Text = savedDetails ?? ""; operationText.Text = savedFeedback ?? text["ready"];
                feedbackPanel.IsExpanded = feedbackOpen;
            }
            Content = Backdrop(outer); rendering = false; ShowStatus();
            if (rebuilding)
                Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
                { (pageHost.Content as ScrollViewer)?.ScrollToVerticalOffset(scrollOffset); languages.Focus(); }));
        }
        // Abby in his VR headset, the app icon's head crop (brand files: LICENSE.md, fan art).
        FrameworkElement Mark(double size)
        {
            var art = Asset("abby-mark.png");
            if (art == null) return new Border { Width = size, Height = size };
            var image = new Image { Source = art, Width = size, Height = size, FlowDirection = FlowDirection.LeftToRight };
            RenderOptions.SetBitmapScalingMode(image, BitmapScalingMode.HighQuality);
            return image;
        }
        // The login-screen backdrop: a night sky with stars, and Abby inside the wave rings above a
        // gold horizon in the free space under the cards. Only the setup page shows Abby; the
        // troubleshooting page is long and its text must not sit on the artwork.
        Image backdropArt;
        UIElement Backdrop(UIElement content)
        {
            var sky = new Grid { Background = new RadialGradientBrush(((SolidColorBrush)Color("#17224A")).Color, ((SolidColorBrush)background).Color)
                { Center = new Point(.5, .8), GradientOrigin = new Point(.5, .8), RadiusX = .8, RadiusY = .62 } };
            var stars = Asset("stars.png");
            if (stars != null)
                sky.Children.Add(new Border { IsHitTestVisible = false, Background = new ImageBrush(stars)
                    { TileMode = TileMode.Tile, Viewport = new Rect(0, 0, 512, 512), ViewportUnits = BrushMappingMode.Absolute, Stretch = Stretch.None, Opacity = .6 } });
            var art = Asset("abby-backdrop.png");
            backdropArt = null;
            if (art != null)
            {
                backdropArt = new Image { Source = art, Height = 420, Stretch = Stretch.Uniform, IsHitTestVisible = false,
                    HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, 0, 52),
                    FlowDirection = FlowDirection.LeftToRight, Visibility = troubleshootingVisible ? Visibility.Collapsed : Visibility.Visible };
                RenderOptions.SetBitmapScalingMode(backdropArt, BitmapScalingMode.HighQuality);
                sky.Children.Add(backdropArt);
            }
            sky.Children.Add(content);
            // After every layout pass (window resize, a notice opening); FitBackdrop only writes on change.
            sky.LayoutUpdated += (s, e) => FitBackdrop();
            return sky;
        }
        // Abby takes the free space under the cards, up to full size; with too little room he steps aside.
        void FitBackdrop()
        {
            var sky = Content as FrameworkElement; var setup = setupPage?.Content as FrameworkElement;
            if (backdropArt == null || sky == null || setup == null || troubleshootingVisible || pageHost?.Content != setupPage) return;
            // DesiredSize: the scroll view stretches the panel to the viewport, the cards end sooner.
            double cardsBottom = setup.TranslatePoint(new Point(0, setup.DesiredSize.Height), sky).Y;
            double room = sky.ActualHeight - backdropArt.Margin.Bottom - cardsBottom - 6;
            double height = Math.Max(1, Math.Min(420, room));
            var shown = room < 150 ? Visibility.Collapsed : Visibility.Visible;
            if (Math.Abs(backdropArt.Height - height) > 0.5) backdropArt.Height = height;
            if (backdropArt.Visibility != shown) backdropArt.Visibility = shown;
        }
        static readonly Dictionary<string, BitmapImage> assets = new Dictionary<string, BitmapImage>();
        // Brand images are embedded in the EXE (WuWaVR.Manager.csproj: assets\*.png).
        static BitmapImage Asset(string name)
        {
            BitmapImage image;
            if (assets.TryGetValue(name, out image)) return image;
            using (var stream = typeof(MainWindow).Assembly.GetManifestResourceStream("assets." + name))
            {
                if (stream == null) return null;
                image = new BitmapImage();
                image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.StreamSource = stream; image.EndInit(); image.Freeze();
            }
            assets[name] = image;
            return image;
        }
        void StyleFooter(Button button)
        { button.Background = Brushes.Transparent; button.Foreground = muted; button.FontSize = 11;
            button.Padding = new Thickness(4, 3, 4, 3); button.Margin = new Thickness(0, 0, 12, 0); }
        Button PageLink(string key, bool troubleshooting)
        {
            var button = new Button { Content = text[key], Tag = key, Cursor = System.Windows.Input.Cursors.Hand };
            StyleFooter(button); button.Click += (s, e) => ShowPage(troubleshooting); actions.Add(button); return button;
        }
        void ShowPage(bool troubleshooting)
        {
            troubleshootingVisible = troubleshooting;
            if (pageHost != null) pageHost.Content = troubleshooting ? troubleshootingPage : setupPage;
            if (backdropArt != null) backdropArt.Visibility = troubleshooting ? Visibility.Collapsed : Visibility.Visible;
            if (!troubleshooting) Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(FitBackdrop));
        }
        ScrollViewer Page(UIElement body)
        { return new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Content = body }; }
        bool WantsInstall() { return store.Selected == null; }
        bool SelectedPackageSupportsSteam()
        {
            if (store.Selected == null) return false;
            // Passive capability check on the installed, package-verified payload.
            // An older helper must never receive a mode it cannot understand.
            string folder = store.Folder(store.Selected);
            return File.Exists(Path.Combine(folder, "app", "dev", "wuwa_game_start.py")) &&
                File.Exists(Path.Combine(folder, "app", "dev", "WuWaSteamStart.ps1"));
        }
        bool SteamChoiceNeedsPackage()
        {
            string pending = store.State.pendingLauncherPath;
            return !String.IsNullOrWhiteSpace(pending) &&
                String.Equals(Path.GetFileName(pending), "Wuthering Waves.exe", StringComparison.OrdinalIgnoreCase) &&
                !SelectedPackageSupportsSteam();
        }
        void OfferSteamPackageUpdate()
        {
            if (!SteamChoiceNeedsPackage()) return;
            // The VR mod row's Update button installs the newer package; the old helper
            // and the game choice are left alone until the player uses it.
            operationText.Text = text["gameSteamUpdateRequired"];
        }
        UIElement Setup()
        {
            var panel = new StackPanel();
            connectionMessage = Label("", 12);
            connectionPanel = Card(text["connectionRecovery"], connectionMessage,
                Wrap(Action("openExisting", async c => { Open(await bridge.OpenConflictAddress(c)); operationText.Text = text["browserOpened"]; }),
                     Action("switchLauncher", async c =>
                     {
                         await bridge.StopConflict(c);
                         await Connect(c);
                         ConnectionRecovered();
                     }, true),
                     Action("retryConnection", async c => { await Connect(c); ConnectionRecovered(); })));
            connectionPanel.Visibility = Visibility.Collapsed; connectionPanel.BorderBrush = Color("#5E3026"); panel.Children.Add(connectionPanel);
            gamePath = Label(text["noGame"], 13, foreground); gamePath.TextWrapping = TextWrapping.NoWrap; gamePath.TextTrimming = TextTrimming.CharacterEllipsis;
            var browse = Action("browse", async c =>
            {
                if (gameLocationOptions.Visibility == Visibility.Visible) { gameLocationOptions.Visibility = Visibility.Collapsed; operationText.Text = text["notChanged"]; return; }
                var found = gameChoicesOverride != null ? await gameChoicesOverride(c) :
                    await Task.Run(() => GameDiscovery.Discover(null, bridge.Data, c, choicesOnly: true), c);
                gameLocations.Items.Clear();
                foreach (var choice in found.Choices)
                {
                    var label = new StackPanel();
                    label.Children.Add(Label(text[choice.Mode == "steam" ? "gameSteamLabel" : "gameOfficialLabel"], 13));
                    var path = Label(choice.Path, 11, muted); path.TextWrapping = TextWrapping.NoWrap; path.TextTrimming = TextTrimming.CharacterEllipsis;
                    label.Children.Add(path);
                    gameLocations.Items.Add(new ComboBoxItem { Content = label, Tag = choice, ToolTip = choice.Path });
                }
                gameLocations.SelectedIndex = found.Choices.Count == 1 ? 0 : -1;
                gameLocations.Visibility = found.Choices.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
                gameLocationOptions.Visibility = Visibility.Visible;
                operationText.Text = text[found.Choices.Count == 0 ? "gameNotFound" : "gameChooseHint"];
            });
            gameLocations = Choice();
            gameLocations.SelectionChanged += (s, e) => { if (!rendering) ShowStatus(); };
            gameLocationOptions = new StackPanel { Visibility = Visibility.Collapsed, Margin = new Thickness(0, 8, 0, 0) };
            gameLocationOptions.Children.Add(Label(text["gameChooseHint"], 12, muted));
            gameLocationOptions.Children.Add(gameLocations);
            gameLocationOptions.Children.Add(Wrap(Action("useGameLocation", async c =>
            {
                var choice = (gameLocations.SelectedItem as ComboBoxItem)?.Tag as GameDiscovery.Choice;
                if (choice == null) throw new InvalidOperationException(text["gameChooseHint"]);
                await ChooseLauncher(choice.Path, c);
            }), Action("browseGameFile", async c =>
            {
                var picker = new OpenFileDialog { Filter = "WuWa launcher (launcher.exe;Wuthering Waves.exe)|launcher.exe;Wuthering Waves.exe", CheckFileExists = true, Title = text["locate"] };
                if (picker.ShowDialog(this) == true) await ChooseLauncher(picker.FileName, c);
                else operationText.Text = text["notChanged"];
            })));
            gameHint = Label(text[preview ? "locateHint" : "findingGame"], 11, muted); gameHint.Margin = new Thickness(0, 0, 0, 2);
            gamePath.Margin = new Thickness(0, 4, 0, 0);
            var gameRow = StatusRow(0, text["rowGame"], browse, gamePath, gameHint, gameLocationOptions);
            runtime = Label(text["runtimeHint"], 13, foreground); runtime.Margin = new Thickness(0, 4, 0, 2);
            runtimeCheck = Action("check", async c => { await EnsureConnected(c); await SubmitJob("/api/check", new { id = CurrentBuild() }, c); });
            runtimeHint = Label(text["runtimeInstallHint"], 11, muted);
            // Headset or simulator is a rare change: its two buttons fold out under Change.
            runtimeOptions = new StackPanel { Visibility = Visibility.Collapsed, Margin = new Thickness(0, 4, 0, 0) };
            runtimeOptions.Children.Add(Wrap(Action("useHeadset", async c => await Runtime("headset", c)), Action("useSimulator", async c => await Runtime("simulator", c))));
            runtimeOptions.Children.Add(runtimeHint);
            var runtimeToggle = Action("runtimeOptions", c =>
            {
                runtimeOptions.Visibility = runtimeOptions.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
                operationText.Text = runtimeOptions.Visibility == Visibility.Visible ? runtimeHint.Text : text["notChanged"];
                return Task.CompletedTask;
            });
            var runtimeRow = StatusRow(2, text["rowHeadset"], runtimeToggle, runtime, runtimeOptions);
            releases = Choice(); foreach (var release in catalog.releases) releases.Items.Add(release);
            if (store.Selected != null && !releases.Items.Cast<Release>().Any(r => r.id == store.Selected.release.id)) releases.Items.Add(store.Selected.release);
            releases.SelectedItem = store.Selected == null ? releases.Items.Cast<Release>().FirstOrDefault() : releases.Items.Cast<Release>().First(r => r.id == store.Selected.release.id);
            catalogState = Label(text[catalogKey], 12, muted);
            packageState = Label(text["none"], 13, foreground);
            gameVersion = Label(text["targetGame"] + " " + SelectedRelease().gameVersion, 12, muted);
            compatible = new CheckBox { Content = Label(text["installConsent"], 12), Foreground = foreground, Margin = new Thickness(0, 3, 0, 0) };
            compatible.Checked += (s, e) => ShowStatus(); compatible.Unchecked += (s, e) => ShowStatus();
            releases.SelectionChanged += (s, e) => { compatible.IsChecked = false; gameVersion.Text = text["targetGame"] + " " + SelectedRelease().gameVersion; if (!rendering) ShowStatus(); };
            versionConsent = compatible;
            var updates = new StackPanel { Margin = new Thickness(0, 7, 0, 0) };
            updates.Children.Add(Label(text["version"], 12, muted)); updates.Children.Add(releases);
            updates.Children.Add(catalogState);
            updates.Children.Add(Wrap(Action("checkUpdates", async c => await CheckUpdates(c)), Action("releaseNotes", c => {
                if (SelectedRelease().channel == "candidate") { details.Text = text["candidateNotes"]; feedbackPanel.IsExpanded = true; operationText.Text = text["candidatePackage"]; }
                else { Open(SelectedRelease().notesUrl); operationText.Text = text["browserOpened"]; }
                return Task.CompletedTask;
            }), Action("installVersion", async c =>
            {
                var release = SelectedRelease();
                if (!Confirm("installVersionConfirm", release.DisplayLabel, release.gameVersion)) return;
                // The dialog names the game version and the account risk: the same
                // consent as the install checkbox, for a version chosen here.
                risk.IsChecked = true;
                await InstallOrSelect(release, c);
            })));
            updates.Children.Add(Label(text["selectedVersionAction"], 11, muted));
            // Every listed version, including diagnostic builds, lives on the
            // troubleshooting page; the VR mod row offers only Update and Back.
            advancedPanel = new Expander { Header = text["advanced"], Foreground = foreground, Content = updates, Margin = new Thickness(0, 0, 0, 14) };
            risk = new CheckBox { Content = Label(text["riskAccept"], 12), Foreground = foreground, Margin = new Thickness(0, 3, 0, 0) };
            risk.Checked += (s, e) => ShowStatus(); risk.Unchecked += (s, e) => ShowStatus();
            riskNotice = Label(text["risk"], 12, Color("#F0B4A3"));
            var consent = new StackPanel(); consent.Children.Add(riskNotice); consent.Children.Add(versionConsent); consent.Children.Add(risk);
            // Shown until the risk is accepted (the helper remembers it) or while installing.
            consentNotice = new Border { Child = consent, Padding = new Thickness(12, 6, 12, 7), CornerRadius = new CornerRadius(9), Background = Color("#22130F"),
                BorderBrush = Color("#5E3026"), BorderThickness = new Thickness(1), Margin = new Thickness(0, 12, 0, 0), Visibility = Visibility.Collapsed };
            packageUpdateButton = Action("updatePackage", async c =>
            {
                var newer = NewerPackage();
                if (newer == null) throw new InvalidOperationException(text["noPackageUpdate"]);
                await InstallOrSelect(newer, c);
            }, true);
            packageRollbackButton = Action("rollbackPackage", async c => await UsePrevious(c));
            foreach (var small in new[] { packageUpdateButton, packageRollbackButton }) { small.Padding = new Thickness(12, 5, 12, 5); small.Margin = new Thickness(0, 4, 8, 2); }
            packageState.Margin = new Thickness(0, 4, 0, 0); gameVersion.FontSize = 11; gameVersion.Margin = new Thickness(0, 0, 0, 2);
            // "Update to X" names the newer package itself; no separate "update available" line.
            var packageRow = StatusRow(1, text["rowMod"], null, packageState, gameVersion, Wrap(packageUpdateButton, packageRollbackButton));
            heroTitle = Label("", 20); heroTitle.FontWeight = FontWeights.SemiBold; heroTitle.Margin = new Thickness(0);
            var launchRow = new DockPanel { Margin = new Thickness(0, 14, 0, 0), LastChildFill = false };
            DockPanel.SetDock(launchButton, Dock.Right); DockPanel.SetDock(playFlatButton, Dock.Right);
            // After a failed launch: the way to the fixes, beside Launch (which is also Try again).
            heroHelp = Action("getHelp", c => { ShowPage(true); return Task.CompletedTask; });
            heroHelp.Padding = new Thickness(16, 12, 16, 12); heroHelp.Margin = new Thickness(0); heroHelp.Visibility = Visibility.Collapsed;
            DockPanel.SetDock(heroHelp, Dock.Left);
            launchRow.Children.Add(launchButton); launchRow.Children.Add(playFlatButton); launchRow.Children.Add(heroHelp);
            var hero = new StackPanel(); hero.Children.Add(heroTitle); hero.Children.Add(launchState); hero.Children.Add(PhaseChips());
            hero.Children.Add(consentNotice); hero.Children.Add(launchRow);
            heroCard = new Border { Background = card, BorderBrush = line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(12),
                Padding = new Thickness(20, 16, 20, 16), Margin = new Thickness(0, 0, 0, 10), Child = hero };
            panel.Children.Add(heroCard);
            var rows = new StackPanel(); Grid.SetIsSharedSizeScope(rows, true); rows.Children.Add(gameRow); rows.Children.Add(Divider()); rows.Children.Add(packageRow); rows.Children.Add(Divider()); rows.Children.Add(runtimeRow);
            panel.Children.Add(new Border { Background = card, BorderBrush = line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(12),
                Padding = new Thickness(18, 4, 18, 4), Child = rows });
            return panel;
        }
        // Starts the game the normal way, with no injection: the official launcher, or Steam.
        async Task PlayWithoutVr(CancellationToken c)
        {
            await Task.Yield();
            var game = Json.Child(status, "game");
            string mode = Json.Text(game, "mode"), path = Json.Text(game, "launcher");
            if (String.IsNullOrEmpty(path)) path = store.State.launcherPath ?? discovery?.Path;
            var choice = GameDiscovery.ExistingChoice(path, File.Exists);
            if (choice == null) throw new InvalidOperationException(text["locateHint"]);
            Open(choice.Mode == "steam" || mode == "steam" ? "steam://rungameid/3513350" : choice.Path);
            operationText.Text = text["playFlatStarted"];
        }
        UIElement Recording()
        {
            source = Choice(text["auto"], text["steamvr"], text["simulator"]);
            source.SelectionChanged += (s, e) => { if (!rendering) ShowStatus(); };
            fps = Choice("30", "45", "60"); size = Choice("720", "1024", "1280", "1536", "2048"); size.SelectedIndex = 1; purpose = Choice(text["videoOnly"], text["playtest"]);
            var panel = new StackPanel(); panel.Children.Add(Label(text["recordInfo"], 14, muted));
            recordingState = Label("", 12, accent); panel.Children.Add(recordingState);
            recordingOptions = new Expander { Header = text["recordOptions"], Foreground = muted, Margin = new Thickness(0, 12, 0, 0), Content = new StackPanel() };
            ((StackPanel)recordingOptions.Content).Children.Add(CaptureFields("source", source, "purpose", purpose));
            ((StackPanel)recordingOptions.Content).Children.Add(CaptureFields("fps", fps, "eyeSize", size));
            // 1.2.5: the video's sound track, what the PC plays (SteamVR recordings).
            recordSound = new CheckBox { Content = Label(text["recordSound"], 12), Foreground = foreground, IsChecked = true, Margin = new Thickness(0, 10, 0, 0) };
            ((StackPanel)recordingOptions.Content).Children.Add(recordSound);
            // 1.2.6: a microphone mixed into that sound (SteamVR recordings). The launcher lists the microphones; the
            // choice is off until one is picked. Separate sound files: each source also alone (game.wav, microphone.wav).
            recordMic = Choice(text["micOff"], text["micDefault"]); recordMicVolume = Choice(MicVolumes.Select(v => v + "%").ToArray());
            recordMicVolume.SelectedIndex = Array.IndexOf(MicVolumes, 100);
            recordMic.SelectionChanged += (s, e) => { if (!rendering) ShowStatus(); };
            ((StackPanel)recordingOptions.Content).Children.Add(CaptureFields("microphone", recordMic, "micVolume", recordMicVolume));
            recordSoundFiles = new CheckBox { Content = Label(text["recordSoundFiles"], 12), Foreground = foreground, IsChecked = true, Margin = new Thickness(0, 10, 0, 0) };
            ((StackPanel)recordingOptions.Content).Children.Add(recordSoundFiles);
            panel.Children.Add(Card(text["record"], Label(text["recordDefaults"], 12, muted),
                Wrap(Action("recordStart", async c =>
                {
                    await EnsureConnected(c);
                    await SubmitJob("/api/record-start", new { videoOnly = purpose.SelectedIndex == 0, fps = int.Parse((string)fps.SelectedItem), eyeWidth = int.Parse((string)size.SelectedItem), sound = recordSound.IsChecked == true, source = new[] { "auto", "steamvr", "simulator" }[source.SelectedIndex],
                        microphone = micIds[Math.Max(0, recordMic.SelectedIndex)], micVolume = MicVolumes[Math.Max(0, recordMicVolume.SelectedIndex)],
                        soundFiles = recordSoundFiles.IsChecked == true }, c);
                }, true), Action("recordStop", async c => { await EnsureConnected(c); await bridge.Post("/api/record-stop", new { }, c); }),
                Action("openRecordings", async c => { await EnsureConnected(c); await bridge.Post("/api/open", new { folder = "recordings" }, c); operationText.Text = text["folderOpened"]; })), recordingOptions));
            panel.Children.Add(Label(text["recordPrivacy"], 12, muted)); return panel;
        }
        // The launcher's microphones (status.recording.microphones) into the choice, keeping the chosen one by its ID;
        // one that went away stays chosen as not connected, and the launcher then explains when recording starts.
        void ShowMicrophones(Dictionary<string, object> capture)
        {
            object value; var listed = new List<KeyValuePair<string, string>>();
            if (capture.TryGetValue("microphones", out value) && value is System.Collections.IList)
                foreach (object entry in (System.Collections.IList)value)
                {
                    var mic = Json.Object(entry); string id = Json.Text(mic, "id"), name = Json.Text(mic, "name");
                    if (Regex.IsMatch(id, @"^\{[0-9.]{1,40}\}\.\{[0-9A-Fa-f-]{36}\}$") && listed.Count < 32)
                        listed.Add(new KeyValuePair<string, string>(id, name.Length > 0 ? name : id));
                }
            string chosen = micIds[Math.Max(0, recordMic.SelectedIndex)];
            string key = string.Join("|", listed.Select(m => m.Key + "=" + m.Value)) + "|" + chosen;
            if (key == micList || recordMic.IsDropDownOpen) return;
            micList = key; bool was = rendering; rendering = true;
            try
            {
                micIds.RemoveRange(2, micIds.Count - 2);
                while (recordMic.Items.Count > 2) recordMic.Items.RemoveAt(2);
                foreach (var mic in listed) { micIds.Add(mic.Key); recordMic.Items.Add(mic.Value); }
                if (!micIds.Contains(chosen)) { micIds.Add(chosen); recordMic.Items.Add(text["micMissing"]); }
                recordMic.SelectedIndex = micIds.IndexOf(chosen);
            }
            finally { rendering = was; }
        }
        UIElement CaptureFields(string leftLabel, ComboBox left, string rightLabel, ComboBox right)
        {
            var grid = new Grid(); grid.ColumnDefinitions.Add(new ColumnDefinition()); grid.ColumnDefinitions.Add(new ColumnDefinition());
            var first = new StackPanel { Margin = new Thickness(0, 0, 10, 0) }; first.Children.Add(Label(text[leftLabel], 12)); first.Children.Add(left);
            var second = new StackPanel { Margin = new Thickness(10, 0, 0, 0) }; second.Children.Add(Label(text[rightLabel], 12)); second.Children.Add(right);
            grid.Children.Add(first); Grid.SetColumn(second, 1); grid.Children.Add(second); return grid;
        }
        UIElement Shortcut(string input, string key)
        {
            var row = new Grid(); row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(132) }); row.ColumnDefinitions.Add(new ColumnDefinition());
            var keys = Label(input, 12, accent); keys.FontWeight = FontWeights.SemiBold; row.Children.Add(keys);
            var description = Label(text[key], 12, muted); Grid.SetColumn(description, 1); row.Children.Add(description); return row;
        }
        UIElement Help()
        {
            var panel = new StackPanel();
            panel.Children.Add(PageLink("backToSetup", false));
            panel.Children.Add(Label(text["troubleshooting"], 22));
            panel.Children.Add(Label(text["fixIntro"], 12, muted));
            installed = Choice(); foreach (var item in store.State.installed) installed.Items.Add(item);
            installed.SelectedItem = store.Selected;
            installed.SelectionChanged += (s, e) => ShowStatus();
            // The three fixes, mildest first. Each confirms or is harmless; the version list
            // they act on (installed) defaults to the version in use, under More options.
            var repair = Action("repair", async c =>
            {
                var item = installed.SelectedItem as Installed;
                if (item == null) throw new InvalidOperationException(text["chooseVersion"]);
                await InstallRelease(item.release, c);
            });
            repair.Content = text["repairFiles"];
            var fixes = new StackPanel(); Grid.SetIsSharedSizeScope(fixes, true);
            fixes.Children.Add(FixRow(repair, "repairInfo"));
            fixes.Children.Add(Divider());
            fixes.Children.Add(FixRow(Action("reset", async c =>
            {
                if (!Confirm("resetConfirm")) return;
                await EnsureConnected(c); await SubmitJob("/api/apply", new { id = CurrentBuild(), reset = true }, c);
            }), "resetInfo"));
            fixes.Children.Add(Divider());
            fixes.Children.Add(FixRow(Action("saveReport", async c => await SaveProblemReport(c)), "saveReportInfo"));
            panel.Children.Add(new Border { Background = card, BorderBrush = line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(12),
                Padding = new Thickness(18, 4, 18, 4), Margin = new Thickness(0, 8, 0, 12), Child = fixes });
            var more = new StackPanel { Margin = new Thickness(0, 6, 0, 0) };
            more.Children.Add(ControllerCheck());
            var web = Action("web", async c => { await EnsureConnected(c); Open(bridge.Address); operationText.Text = text["webOpened"]; });
            web.Content = text["developerTools"];
            more.Children.Add(Wrap(runtimeCheck, Action("copyDiagnostics", async c => await CopyDiagnostics(c)), GuideLink("report", "feedback.html"),
                Action("openSettings", c => { OpenSettingsFolder(); return Task.CompletedTask; }), web));
            more.Children.Add(ProcessRecovery());
            more.Children.Add(advancedPanel);
            var recovery = new StackPanel { Margin = new Thickness(8) };
            recovery.Children.Add(Card(text["installed"], installed, Label(text["recoveryInfo"], 13, muted),
                Wrap(Action("useVersion", async c =>
                {
                    var item = installed.SelectedItem as Installed; if (item == null) throw new InvalidOperationException(text["chooseVersion"]);
                    await Task.Run(() => PackageStore.Verify(store.Folder(item), item.release, c), c);
                    await PreparePackageChange(c); store.Select(item); PopulateInstalled(); await Connect(c);
                }), Action("rollback", async c => await UsePrevious(c)),
                Action("remove", async c => await Remove(c)), Action("removeOld", async c => await RemoveOld(c)),
                // Not a fix (the next launch applies the mod again): for leaving the mod, going back
                // to another UEVR profile, or before removing the version in use. Uninstall runs it too.
                Action("restore", async c =>
                {
                    if (!Confirm("restoreConfirm")) return;
                    await EnsureConnected(c); await SubmitJob("/api/restore", new { }, c);
                }))));
            recovery.Children.Add(Wrap(Action("scanGame", async c => await DiscoverGame(c)), Action("openLogs", c =>
            {
                if (!Directory.Exists(bridge.Data)) throw new DirectoryNotFoundException(text["logsMissing"]);
                Open(bridge.Data); operationText.Text = text["folderOpened"]; return Task.CompletedTask;
            }), Action("portable", async c => await OpenPortable(c))));
            recoveryPanel = new Expander { Header = text["recovery"], Foreground = foreground, Content = recovery, Margin = new Thickness(0, 0, 0, 14) };
            more.Children.Add(recoveryPanel);
            uninstallResultText = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
                MinHeight = 50, MaxHeight = 200, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = card, Foreground = foreground, BorderThickness = new Thickness(0), Padding = new Thickness(8), Text = uninstallDetails,
                Visibility = String.IsNullOrWhiteSpace(uninstallDetails) ? Visibility.Collapsed : Visibility.Visible };
            uninstallPanel = new Expander { Header = text["prepareUninstall"], Foreground = foreground, Margin = new Thickness(0, 0, 0, 14),
                Content = Card(text["prepareUninstall"], Label(text["uninstallIntro"], 12, muted),
                    Wrap(ControllerButton("prepareUninstall", async () => {
                        await Run(PrepareUninstall); if (uninstallNeedsRecovery) OpenProcessRecovery();
                    })), uninstallResultText) };
            more.Children.Add(uninstallPanel);
            morePanel = new Expander { Header = text["moreOptions"], Foreground = foreground, Content = more, Margin = new Thickness(0, 0, 0, 12) };
            panel.Children.Add(morePanel);
            panel.Children.Add(Label(text["privacy"], 12, muted)); return panel;
        }
        UIElement FixRow(Button action, string infoKey)
        {
            var grid = new Grid { Margin = new Thickness(0, 9, 0, 9) };
            // The buttons share the widest label's width (German runs long); the fixes' panel is the scope.
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto, MinWidth = 210, SharedSizeGroup = "fixButton" }); grid.ColumnDefinitions.Add(new ColumnDefinition());
            action.Margin = new Thickness(0, 0, 14, 0); action.HorizontalAlignment = HorizontalAlignment.Stretch; action.VerticalAlignment = VerticalAlignment.Center;
            grid.Children.Add(action);
            var info = Label(text[infoKey], 12, muted); info.VerticalAlignment = VerticalAlignment.Center; info.Margin = new Thickness(0);
            Grid.SetColumn(info, 1); grid.Children.Add(info); return grid;
        }
        // UEVR's own per-game folder (config.txt, log.txt): players look for it under the mod's
        // name and miss UnrealVRMod (Lelouche, 1 Nov).
        Func<string> settingsFolderOverride = null;
        string SettingsFolder()
        {
            return settingsFolderOverride != null ? settingsFolderOverride() :
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "UnrealVRMod", "Client-Win64-Shipping");
        }
        void OpenSettingsFolder()
        {
            string folder = SettingsFolder();
            if (!Directory.Exists(folder)) throw new DirectoryNotFoundException(text["settingsMissing"]);
            Open(folder); operationText.Text = text["folderOpened"];
        }
        // Uninstalling leaves no mod settings in UEVR's WuWa profile: it goes back to how it was
        // before WuWa VR first changed it (Restore-WuWaOriginalProfile backs the mod's up first).
        // If that fails nothing is removed, so the player can close the game and try again.
        async Task RestoreBeforeUninstall(CancellationToken c)
        {
            if (store.Selected == null) return;
            if (!connectionReady) await EnsureConnected(c);
            status = await bridge.Status(c);
            if (!Json.Flag(status, "originalBackup") ||
                (!String.IsNullOrEmpty(Json.Text(status, "restoredOriginalAt")) && String.IsNullOrEmpty(Json.Text(status, "selected")))) return;
            operationText.Text = text["uninstallRestoring"];
            await bridge.Post("/api/restore", new { }, c);
            for (var waited = Stopwatch.StartNew(); waited.Elapsed < TimeSpan.FromMinutes(2); await Task.Delay(400, c))
            {
                status = await bridge.Status(c); var job = Json.Child(status, "job");
                if (Json.Text(job, "kind") != "restore" || Json.Flag(job, "running")) continue;
                if (!Json.Flag(job, "error")) return;
                string output = Json.Text(job, "output").Trim();
                throw new InvalidOperationException(text["uninstallRestoreFailed"] + (output == "" ? "" : Environment.NewLine + output));
            }
            throw new TimeoutException(text["uninstallRestoreFailed"]);
        }
        async Task PrepareUninstall(CancellationToken c)
        {
            uninstallNeedsRecovery = false;
            string prompt = String.Format(text["uninstallConfirm"], Path.Combine(store.Root, "versions"), store.Cache) +
                Environment.NewLine + Environment.NewLine + text["uninstallKeep"];
            bool confirmed = uninstallConfirmOverride != null ? uninstallConfirmOverride(prompt) :
                MessageBox.Show(this, prompt, text["prepareUninstall"], MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) == MessageBoxResult.Yes;
            if (!confirmed) { operationText.Text = text["notChanged"]; return; }
            UninstallResult result = null;
            try
            {
                await RestoreBeforeUninstall(c);
                operationText.Text = text["uninstallPreparing"];
                // The same service is called by Windows uninstall. It validates
                // ownership, closes only idle helpers and enforces its own budget.
                result = await Task.Run(() => uninstallPrepareOverride != null ? uninstallPrepareOverride(store.Root, bridge.Data, c) :
                    LauncherUninstall.PrepareAsync(store.Root, bridge.Data, c), c);
                if (result == null) throw new InvalidDataException(text["uninstallFailed"]);
                bool retained = result.RetainedReasons != null && result.RetainedReasons.Count != 0;
                uninstallDetails = text[result.Success && !retained ? "uninstallReady" : "uninstallRetained"] + Environment.NewLine +
                    String.Format(text["uninstallRemoved"], result.RemovedFiles) + Environment.NewLine + result.Message;
                if (retained) uninstallDetails += Environment.NewLine + String.Join(Environment.NewLine, result.RetainedReasons);
                if (!String.IsNullOrWhiteSpace(result.ReportPath)) uninstallDetails += Environment.NewLine + String.Format(text["uninstallReport"], result.ReportPath);
                uninstallDetails += Environment.NewLine + Environment.NewLine + text["uninstallNext"];
                operationText.Text = text[result.Success && !retained ? "uninstallReady" : "uninstallRetained"];
            }
            catch (Exception error)
            {
                uninstallDetails = text["uninstallFailed"] + Environment.NewLine + error.Message;
                throw;
            }
            finally
            {
                // Cleanup may have stopped the helper or removed its package even
                // on a partial result. Never keep an old connection or store state.
                bridge.ForgetConnection(); Disconnected();
                store = new PackageStore(store.Root); PopulateInstalled();
                uninstallResultText.Text = uninstallDetails; uninstallResultText.Visibility = Visibility.Visible;
                uninstallPanel.IsExpanded = true;
                uninstallNeedsRecovery = result == null || !result.Success;
            }
        }
        Button ControllerButton(string key, Func<Task> action)
        {
            var button = new Button { Content = text[key], Tag = key, Padding = new Thickness(14, 9, 14, 9),
                Margin = new Thickness(0, 4, 8, 4), Cursor = System.Windows.Input.Cursors.Hand };
            ToolTipService.SetShowOnDisabled(button, true);
            button.Click += async (s, e) => { if (!preview && !closing) await action(); };
            actions.Add(button); return button;
        }
        UIElement ProcessRecovery()
        {
            processRecoveryState = Label("", 12, accent); processRecoveryRows = new StackPanel();
            var rows = new ScrollViewer { Content = processRecoveryRows, MaxHeight = 300,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
            processRecoveryPanel = new Expander { Header = text["processRecoveryTitle"], Foreground = foreground, Margin = new Thickness(0, 6, 0, 12),
                Content = Card(text["processRecoveryTitle"], Label(text["processRecoveryIntro"], 12, muted),
                    Wrap(Action("processRecoveryScan", ScanStuckProcesses), Action("processRecoveryStop", StopStuckProcesses)), processRecoveryState, rows) };
            DrawProcessRecoveryRows(); return processRecoveryPanel;
        }
        string RecoveryRole(RecoveryCandidate item)
        {
            return text[item.Kind == "Startup worker" ? "processRoleStartup" : item.Kind == "Launcher helper" ? "processRoleHelper" :
                item.Kind == "Runtime/profile worker" ? "processRoleRuntime" : item.Kind == "Recorded launcher owner" ? "processRoleRecorded" : "processRoleUnknown"];
        }
        string RecoveryIdentity(RecoveryCandidate item)
        { return "PID " + item.Pid + " · " + RecoveryRole(item) + " · " + String.Format(text["processRecoveryStarted"], item.StartedUtc ?? "?"); }
        void DrawProcessRecoveryRows()
        {
            if (processRecoveryRows == null) return;
            processRecoveryRows.Children.Clear();
            if (processRecoveryReport != null && processRecoveryReport.Candidates != null)
                foreach (var item in processRecoveryReport.Candidates)
                {
                    var row = new StackPanel { Margin = new Thickness(0, 7, 0, 7) };
                    var select = new CheckBox { Tag = item, Content = Label(RecoveryIdentity(item), 12), Foreground = foreground,
                        IsChecked = processRecoverySelected.Contains(item.Pid), IsEnabled = operation == null && processRecoveryFresh && item.Eligible };
                    select.Checked += (s, e) => { if (item.Eligible && processRecoveryFresh) processRecoverySelected.Add(item.Pid); ShowStatus(); };
                    select.Unchecked += (s, e) => { processRecoverySelected.Remove(item.Pid); ShowStatus(); };
                    row.Children.Add(select);
                    row.Children.Add(Label((item.Executable ?? "") + (String.IsNullOrWhiteSpace(item.Script) ? "" : Environment.NewLine + item.Script), 11, muted));
                    string outcome = String.IsNullOrEmpty(item.Outcome) ? text[item.Eligible ? "processRecoveryEligible" : "processRecoveryExcluded"] :
                        text[item.Outcome == "Stopped" ? "processOutcomeStopped" : item.Outcome == "Exited" ? "processOutcomeExited" : item.Outcome == "Refused" ? "processOutcomeRefused" : "processOutcomeFailed"];
                    row.Children.Add(Label(outcome + (String.IsNullOrWhiteSpace(item.Reason) ? "" : ": " + item.Reason), 11, muted));
                    processRecoveryRows.Children.Add(row);
                }
            UpdateProcessRecovery();
        }
        void UpdateProcessRecovery()
        {
            if (processRecoveryState == null) return;
            processRecoveryState.Text = text[processRecoveryNotice] +
                (String.IsNullOrWhiteSpace(processRecoveryReport?.Message) ? "" : Environment.NewLine + processRecoveryReport.Message);
            foreach (var row in processRecoveryRows.Children.OfType<StackPanel>())
                foreach (var select in row.Children.OfType<CheckBox>())
                    select.IsEnabled = operation == null && processRecoveryFresh && ((RecoveryCandidate)select.Tag).Eligible;
        }
        async Task ScanStuckProcesses(CancellationToken c)
        {
            processRecoveryFresh = false; processRecoverySelected.Clear(); processRecoveryReport = null;
            processRecoveryNotice = "processRecoveryScanning"; DrawProcessRecoveryRows();
            try
            {
                processRecoveryReport = await (processRecoveryScanOverride != null ? processRecoveryScanOverride(c) :
                    LauncherProcessRecovery.ScanAsync(store.Root, bridge.Data, store.State.installed.Select(item => store.Folder(item)).ToArray(), c));
                if (processRecoveryReport == null) throw new InvalidDataException(text["processRecoveryFailed"]);
                processRecoveryFresh = !processRecoveryReport.Cancelled;
                processRecoveryNotice = processRecoveryReport.Cancelled ? "processRecoveryCancelled" :
                    processRecoveryReport.Candidates == null || processRecoveryReport.Candidates.Count == 0 ? "processRecoveryEmpty" : "processRecoveryChoose";
                operationText.Text = text[processRecoveryNotice];
            }
            catch { processRecoveryNotice = "processRecoveryFailed"; throw; }
            finally { DrawProcessRecoveryRows(); }
        }
        async Task StopStuckProcesses(CancellationToken c)
        {
            var selected = processRecoveryReport?.Candidates?.Where(item => item.Eligible && processRecoverySelected.Contains(item.Pid)).ToArray();
            if (!processRecoveryFresh || selected == null || selected.Length == 0) throw new InvalidOperationException(text["processRecoveryChoose"]);
            string prompt = text["processRecoveryConfirm"] + Environment.NewLine + Environment.NewLine +
                String.Join(Environment.NewLine, selected.Select(RecoveryIdentity)) + Environment.NewLine + Environment.NewLine + text["processRecoveryEffect"];
            bool confirmed = processRecoveryConfirmOverride != null ? processRecoveryConfirmOverride(prompt) :
                MessageBox.Show(this, prompt, text["processRecoveryStop"], MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) == MessageBoxResult.Yes;
            if (!confirmed) { operationText.Text = text["notChanged"]; return; }
            processRecoveryFresh = false; processRecoverySelected.Clear();
            processRecoveryNotice = "processRecoveryStopping"; DrawProcessRecoveryRows();
            var pids = selected.Select(item => item.Pid).ToArray();
            try
            {
                processRecoveryReport = await (processRecoveryStopOverride != null ? processRecoveryStopOverride(processRecoveryReport, pids, c) :
                    LauncherProcessRecovery.StopAsync(processRecoveryReport, pids, c));
                if (processRecoveryReport == null) throw new InvalidDataException(text["processRecoveryFailed"]);
                bool allStopped = pids.All(pid => processRecoveryReport.Candidates != null && processRecoveryReport.Candidates.Any(item =>
                    item.Pid == pid && (item.Outcome == "Stopped" || item.Outcome == "Exited")));
                processRecoveryNotice = processRecoveryReport.Cancelled ? "processRecoveryCancelled" : allStopped ? "processRecoveryStopped" : "processRecoveryPartial";
                operationText.Text = text[processRecoveryNotice] + " " + text["processRecoveryReconnect"];
            }
            catch { processRecoveryNotice = "processRecoveryFailed"; throw; }
            finally
            {
                // A stop request may partially succeed even when elevation/cancellation
                // prevents receiving its final report. Never keep the old attachment.
                bridge.ForgetConnection(); Disconnected(); connectionProblem = text["processRecoveryReconnect"];
                DrawProcessRecoveryRows();
            }
        }
        UIElement ControllerCheck()
        {
            controllerState = Label("", 13, accent);
            controllerProgress = new ProgressBar { Height = 3, IsIndeterminate = true, Foreground = accent, Background = card,
                Margin = new Thickness(0, 5, 0, 6), Visibility = Visibility.Collapsed };
            controllerDetails = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
                MinHeight = 90, MaxHeight = 210, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = background, Foreground = foreground, BorderThickness = new Thickness(0), Padding = new Thickness(10), FontSize = 12 };
            var guidance = new Expander { Header = text["controllerGuidance"], Foreground = foreground, Margin = new Thickness(0, 8, 0, 0),
                Content = new StackPanel() };
            var steps = (StackPanel)guidance.Content;
            steps.Children.Add(Label(text["controllerOnlyTitle"], 13, accent)); steps.Children.Add(Label(text["controllerOnlyHelp"], 12, muted));
            steps.Children.Add(Label(text["controllerTreadmillTitle"], 13, accent)); steps.Children.Add(Label(text["controllerTreadmillHelp"], 12, muted));
            controllerPanel = new Expander { Header = text["controllerCheck"], Foreground = foreground, Margin = new Thickness(0, 0, 0, 10),
                Content = Card(text["controllerCheck"], Label(text["controllerIntro"], 12, muted), controllerState, controllerProgress,
                    Wrap(ControllerButton("controllerRefresh", RefreshControllers), ControllerButton("controllerCopy", CopyControllerReport)),
                    controllerDetails, Label(text["controllerPrivacy"], 11, muted), guidance) };
            return controllerPanel;
        }
        async Task RefreshControllers()
        {
            if (controllerChecking || closing || preview) return;
            controllerReport = null; controllerFailureKey = null; controllerNoticeKey = null;
            controllerCheckedAt = DateTime.UtcNow; controllerChecking = true;
            var cancel = new CancellationTokenSource(); controllerOperation = cancel;
            ShowStatus();
            try
            {
                Func<CancellationToken, Task<ControllerDiagnostics.Report>> probe = controllerProbeOverride ?? ControllerDiagnostics.RunAsync;
                // Keep even probe startup off the dispatcher. An outer deadline
                // also bounds unexpected provider failures without blocking Help.
                var pending = Task.Run(() => probe(cancel.Token));
                var deadline = Task.Delay(controllerTimeout, cancel.Token);
                if (await Task.WhenAny(pending, deadline) != pending)
                {
                    cancel.Cancel();
                    _ = pending.ContinueWith(t => { var ignored = t.Exception; }, CancellationToken.None,
                        TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                    if (!closing) controllerFailureKey = "controllerTimedOut";
                }
                else
                {
                    var result = await pending;
                    if (!closing) { controllerReport = result; if (result == null) controllerFailureKey = "controllerFailed"; }
                }
            }
            catch (OperationCanceledException) { if (!closing) controllerFailureKey = "controllerTimedOut"; }
            catch (Exception) { if (!closing) controllerFailureKey = "controllerFailed"; }
            finally
            {
                controllerChecking = false; controllerOperation = null; cancel.Dispose();
                if (!closing) ShowStatus();
            }
        }
        string ControllerReportText()
        {
            if (controllerReport != null) return text.ControllerReport(controllerReport);
            if (controllerFailureKey == null) return text["controllerNotChecked"];
            return text["controllerReportTitle"] + Environment.NewLine +
                String.Format(text["controllerCheckedAt"], controllerCheckedAt.ToString("yyyy-MM-dd HH:mm:ss 'UTC'")) +
                Environment.NewLine + text[controllerFailureKey] + Environment.NewLine + text["controllerPrivacy"];
        }
        Task CopyControllerReport()
        {
            if (controllerChecking || (controllerReport == null && controllerFailureKey == null)) return Task.CompletedTask;
            try
            {
                string report = ControllerReportText();
                if (controllerCopyOverride != null) controllerCopyOverride(report); else Clipboard.SetText(report);
                controllerNoticeKey = "controllerCopied";
            }
            catch (Exception) { controllerNoticeKey = "controllerCopyFailed"; }
            UpdateControllerCheck(); return Task.CompletedTask;
        }
        void UpdateControllerCheck()
        {
            if (controllerState == null) return;
            controllerState.Text = text[controllerChecking ? "controllerChecking" : controllerNoticeKey ?? controllerFailureKey ??
                (controllerReport == null ? "controllerNotChecked" : "controllerComplete")];
            controllerProgress.Visibility = controllerChecking ? Visibility.Visible : Visibility.Collapsed;
            controllerDetails.Text = controllerChecking ? text["controllerCheckingHint"] : ControllerReportText();
            controllerDetails.Visibility = controllerReport != null || controllerFailureKey != null ? Visibility.Visible : Visibility.Collapsed;
            foreach (var button in actions.Where(b => (string)b.Tag == "controllerRefresh" || (string)b.Tag == "controllerCopy"))
            {
                bool canCopy = controllerReport != null || controllerFailureKey != null;
                button.IsEnabled = !controllerChecking && ((string)button.Tag == "controllerRefresh" || canCopy);
                button.ToolTip = button.IsEnabled ? null : text[controllerChecking ? "controllerChecking" : "controllerNotChecked"];
            }
        }
        void ConnectionRecovered()
        {
            // Keep lastNativeError for copied diagnostics, but retire the old
            // connection failure from the successfully recovered setup screen.
            details.Clear(); feedbackPanel.IsExpanded = false;
            operationText.Text = text["launcherRecovered"];
        }
        async Task OpenPortable(CancellationToken c)
        {
            var release = store.Selected == null ? SelectedRelease() : store.Selected.release;
            var archive = await Task.Run(() => RepoClient.BundledArchive(release, Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "packages"), c), c);
            if (archive == null)
            {
                var cached = Paths.Inside(store.Cache, release.sha256.ToLowerInvariant() + ".zip");
                if (!File.Exists(cached)) throw new FileNotFoundException(text["portableMissing"]);
                bool valid = await Task.Run(() => new FileInfo(cached).Length == release.size && RepoClient.Hash(cached) == release.sha256.ToLowerInvariant(), c);
                if (!valid) throw new InvalidDataException(text["portableInvalid"]);
                archive = cached;
            }
            c.ThrowIfCancellationRequested(); Open(Path.GetDirectoryName(archive));
            operationText.Text = text["portableHint"];
            details.Text = text["portableHint"] + Environment.NewLine + release.id + Environment.NewLine + archive;
        }
        Action<string> diagnosticsCopyOverride = null;
        const string SecretLines = @"(?im)(\b(?:user(?:name)?|computer(?:name)?|password|passwd|secret|token|api[ _-]?key|authorization|serial(?:[ _-]?number)?|device[ _-]?(?:id|instance))\b[""']?\s*[:=]\s*)[^\r\n]*";
        const string DeviceIds = @"(?i)\b(?:HID|USB|BTHENUM|PCI)[\\#][^\s""'<>]+";
        static string RedactNames(string report)
        {
            foreach (var value in new[] { Environment.UserName, Environment.MachineName })
                if (!String.IsNullOrWhiteSpace(value))
                    report = Regex.Replace(report, @"(?<![\w])" + Regex.Escape(value) + @"(?![\w])", "[private]", RegexOptions.IgnoreCase);
            return report;
        }
        string RedactDiagnostics(string report)
        {
            // Apply this to the complete report, including native exceptions and
            // helper logs. A helper's own redaction is not a clipboard boundary.
            report = Regex.Replace(report ?? "", SecretLines, "$1[redacted]");
            report = Regex.Replace(report, @"(?i)(?:file:/+|[a-z]:[\\/]|\\\\|%(?:USERPROFILE|LOCALAPPDATA|APPDATA)%[\\/])[^\r\n""'<>|]*", "[path]");
            report = Regex.Replace(report, DeviceIds, "[device]");
            return RedactNames(report);
        }
        // Files in a problem report keep their paths readable (which runtime, which game copy) but
        // not whose PC they came from; a game account number is hidden too.
        static string RedactReportFile(string content)
        {
            string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            if (!String.IsNullOrEmpty(home)) content = Regex.Replace(content, Regex.Escape(home), "%USERPROFILE%", RegexOptions.IgnoreCase);
            content = Regex.Replace(content, SecretLines, "$1[redacted]");
            content = Regex.Replace(content, DeviceIds, "[device]");
            content = Regex.Replace(content, @"(?i)\b(UID\W{0,3})\d{6,12}\b", "$1[hidden]");
            return RedactNames(content);
        }
        // The last part of a file another program may still be writing (the game keeps log.txt open).
        static string ReadTail(string path, int limit)
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            {
                long start = Math.Max(0, stream.Length - limit); stream.Seek(start, SeekOrigin.Begin);
                var bytes = new byte[stream.Length - start]; int read = 0;
                while (read < bytes.Length) { int n = stream.Read(bytes, read, bytes.Length - read); if (n <= 0) break; read += n; }
                return (start > 0 ? "[earlier part left out]" + Environment.NewLine : "") + System.Text.Encoding.UTF8.GetString(bytes, 0, read);
            }
        }
        Func<string> reportFolderOverride = null;
        // One zip on the desktop with what a bug report needs: the copied diagnostics, the last
        // three launch transcripts, UEVR's config.txt and log.txt and the launcher logs. It is
        // written beside its final name first, so a failed report never leaves half a zip.
        async Task SaveProblemReport(CancellationToken c)
        {
            string diagnostics = (await DiagnosticsText(c)).Item1;
            string folder = reportFolderOverride != null ? reportFolderOverride() : Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            string target = Path.Combine(folder, "WuWa VR problem report " + DateTime.Now.ToString("yyyy-MM-dd HHmmss", CultureInfo.InvariantCulture) + ".zip");
            string data = bridge.Data, settings = SettingsFolder();
            await Task.Run(() =>
            {
                var files = new List<KeyValuePair<string, string>>();
                var runs = new DirectoryInfo(Path.Combine(data, "runs"));
                if (runs.Exists)
                    foreach (var run in runs.GetDirectories().OrderByDescending(d => d.Name, StringComparer.Ordinal).Take(3))
                        foreach (var name in new[] { "startup.log", "startup.json", "build.json", "openxr.json" })
                            files.Add(new KeyValuePair<string, string>("launches/" + run.Name + "/" + name, Path.Combine(run.FullName, name)));
                var logs = new DirectoryInfo(Path.Combine(data, "logs"));
                if (logs.Exists)
                    foreach (var log in logs.GetFiles("launcher-*.log").OrderByDescending(f => f.Name, StringComparer.Ordinal).Take(3))
                        files.Add(new KeyValuePair<string, string>("launcher/" + log.Name, log.FullName));
                foreach (var name in new[] { "config.txt", "log.txt" })
                    files.Add(new KeyValuePair<string, string>("uevr/" + name, Path.Combine(settings, name)));
                string partial = target + ".partial";
                try
                {
                    using (var zip = System.IO.Compression.ZipFile.Open(partial, System.IO.Compression.ZipArchiveMode.Create))
                    {
                        Action<string, string> add = (name, content) =>
                        {
                            using (var writer = new StreamWriter(zip.CreateEntry(name).Open(), new System.Text.UTF8Encoding(false))) writer.Write(content);
                        };
                        add("report.txt", diagnostics);
                        foreach (var file in files)
                        {
                            c.ThrowIfCancellationRequested();
                            if (File.Exists(file.Value)) add(file.Key, RedactReportFile(ReadTail(file.Value, 4 << 20)));
                        }
                    }
                    File.Move(partial, target);
                }
                finally { if (File.Exists(partial)) File.Delete(partial); }
            }, c);
            if (!preview) { if (openOverride != null) openOverride(target); else Process.Start("explorer.exe", "/select,\"" + target + "\""); }
            operationText.Text = String.Format(text["reportSaved"], Path.GetFileName(target));
        }
        async Task<Tuple<string, bool>> DiagnosticsText(CancellationToken c)
        {
            string report = text["nativeDiagnostics"] + Environment.NewLine +
                (String.IsNullOrEmpty(lastNativeError) ? text["noNativeError"] : lastNativeError);
            if (!String.IsNullOrWhiteSpace(details.Text)) report += Environment.NewLine + Environment.NewLine + text["details"] + Environment.NewLine + details.Text;
            bool backendAvailable = false;
            if (bridge.Address != null)
            {
                try { report += Environment.NewLine + Environment.NewLine + await bridge.Diagnostics(c); backendAvailable = true; }
                catch (OperationCanceledException) when (c.IsCancellationRequested) { throw; }
                catch (Exception e) { report += Environment.NewLine + Environment.NewLine + text["backendUnavailable"] + ": " + e.Message; }
            }
            else report += Environment.NewLine + Environment.NewLine + text["backendUnavailable"];
            c.ThrowIfCancellationRequested();
            return Tuple.Create(RedactDiagnostics(report), backendAvailable);
        }
        async Task CopyDiagnostics(CancellationToken c)
        {
            var report = await DiagnosticsText(c);
            if (diagnosticsCopyOverride != null) diagnosticsCopyOverride(report.Item1); else Clipboard.SetText(report.Item1);
            operationText.Text = text[report.Item2 ? "diagnosticsCopied" : "diagnosticsLocalOnly"];
        }
        static string JobSnapshot(Dictionary<string, object> status)
        {
            var job = Json.Child(status, "job");
            var launch = LauncherBridge.CurrentLaunch(status) ? Json.Child(status, "launch") : new Dictionary<string, object>();
            return Json.Write(new { kind = Json.Text(job, "kind"), running = Json.Flag(job, "running"),
                message = Json.Text(job, "message"), output = Json.Text(job, "output"), error = Json.Flag(job, "error"), code = Json.Text(job, "code"),
                attempt = Json.Text(launch, "attemptId"), phase = Json.Text(launch, "phase"), workerRunning = Json.Flag(launch, "running"),
                stalled = Json.Flag(launch, "stalled"), unknown = Json.Flag(launch, "activityUnknown"),
                workerMessage = Json.Text(launch, "message"), cancellable = Json.Flag(launch, "cancellable") });
        }
        async Task SubmitJob(string path, object body, CancellationToken c)
        {
            if (path == "/api/launch") unresolvedStartup = true;
            await bridge.Post(path, body, c);
            submittedJob = true; operationText.Text = text["requestSent"];
            // A job can finish between polls with exactly the same result as its predecessor.
            lastJobSnapshot = null;
        }
        Func<string, bool> confirmationOverride = null;
        bool Confirm(string key, params object[] args)
        {
            string message = args.Length == 0 ? text[key] : String.Format(text[key], args);
            bool accepted = confirmationOverride != null ? confirmationOverride(key) : MessageBox.Show(this, message, text["title"], MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
            if (!accepted) operationText.Text = text["notChanged"];
            return accepted;
        }
        // A three-way question with named buttons: 1 = first (default), 0 = second, -1 = cancel.
        // A message box would label its buttons in the Windows language, not the app's.
        Func<string, int> choiceOverride = null;
        int Choose(string key, string first, string second, params object[] args)
        {
            if (choiceOverride != null) return choiceOverride(key);
            int result = -1;
            var dialog = new Window { Title = text["title"], Owner = this, SizeToContent = SizeToContent.WidthAndHeight, ResizeMode = ResizeMode.NoResize,
                WindowStartupLocation = WindowStartupLocation.CenterOwner, ShowInTaskbar = false, Background = background, Foreground = foreground,
                FontFamily = FontFamily, FontSize = FontSize };
            dialog.Resources.Add(typeof(Button), ButtonStyle());
            var body = new StackPanel { Margin = new Thickness(22, 18, 22, 14), MaxWidth = 480 };
            body.Children.Add(Label(String.Format(text[key], args), 14));
            body.Children.Add(Label(text["launchOfferNote"], 12, muted));
            Func<string, int, bool, Button> option = (label, value, primary) =>
            {
                var button = new Button { Content = label, Padding = new Thickness(14, 9, 14, 9), Margin = new Thickness(0, 10, 8, 0), IsDefault = primary,
                    Background = primary ? accent : Color("#1B2444"), Foreground = primary ? background : foreground, Cursor = System.Windows.Input.Cursors.Hand };
                button.Click += (s, e) => { result = value; dialog.Close(); };
                return button;
            };
            var cancel = option(text["cancel"], -1, false); cancel.IsCancel = true;
            body.Children.Add(Wrap(option(first, 1, true), option(second, 0, false), cancel));
            dialog.Content = body; dialog.ShowDialog();
            return result;
        }
        // Launch in VR offers an update that is waiting, once per version per session,
        // and never blocks: the automatic install at start is skipped while the game
        // runs, when the check failed, or when a version came out after start.
        // Returns false when this click should not launch.
        async Task<bool> OfferUpdateBeforeLaunch(CancellationToken c)
        {
            string app = launcherUpdates.AvailableVersion;
            if (app != null && launchOffers.Add("app " + app))
            {
                int answer = Choose(launcherUpdates.ReadyToRestart ? "launchAppUpdateReady" : "launchAppUpdate", text["launchAppRestart"], text["launchNow"],
                    app, launcherUpdates.CurrentVersion);
                if (answer < 0) { launchOffers.Remove("app " + app); operationText.Text = text["notChanged"]; return false; }
                if (answer > 0) { await UpdateLauncherNow(c); return false; }
            }
            // The same rule as the automatic install: public packages only, the same
            // game version, and never a version already installed (going back is a choice).
            var newer = AutoPackageUpdate();
            if (newer != null && launchOffers.Add(newer.id))
            {
                string current = store.Selected.release.DisplayLabel;
                int answer = Choose("launchPackageUpdate", text["launchUpdateFirst"], String.Format(text["launchCurrent"], current), newer.DisplayLabel, current);
                if (answer < 0) { launchOffers.Remove(newer.id); operationText.Text = text["notChanged"]; return false; }
                if (answer > 0)
                {
                    await InstallOrSelect(newer, c);
                    // The new package's runtime may differ (its own simulator): check again.
                    status = await bridge.Status(c);
                    var block = LauncherPresentation.RuntimeLaunchBlock(connectionReady, status);
                    if (block != null) throw new InvalidOperationException(text[block]);
                }
            }
            return true;
        }
        Release SelectedRelease() { return releases.SelectedItem as Release ?? catalog.releases[0]; }
        string CurrentBuild() { return store.Selected.release.buildId; }
        async Task ChooseLauncher(string path, CancellationToken c)
        {
            var selection = GameDiscovery.ExistingChoice(path, File.Exists);
            if (selection == null) throw new InvalidOperationException(text["locateHint"]);
            string chosen = selection.Path;
            string previous = store.State.launcherPath, pending = store.State.pendingLauncherPath;
            store.State.launcherPath = chosen; store.State.pendingLauncherPath = chosen;
            try { store.Save(); }
            catch { store.State.launcherPath = previous; store.State.pendingLauncherPath = pending; throw; }
            await DiscoverGame(c);
            OfferSteamPackageUpdate();
            if (connectionReady) await Connect(c);
            gameLocationOptions.Visibility = Visibility.Collapsed;
        }
        async Task DiscoverGame(CancellationToken c)
        {
            gameHint.Text = text["findingGame"];
            discovery = await Task.Run(() => GameDiscovery.Discover(store.State.launcherPath, bridge.Data, c, store.State.pendingLauncherPath), c);
            ShowStatus(); operationText.Text = gameHint.Text;
        }
        async Task EnsureConnected(CancellationToken c) { if (store.Selected == null) throw new InvalidOperationException(text["chooseVersion"]); await Connect(c); }
        async Task Connect(CancellationToken c)
        {
            if (store.Selected == null) throw new InvalidOperationException(text["chooseVersion"]);
            connectionReady = false;
            try
            {
                string pending = store.State.pendingLauncherPath;
                var pendingSelection = GameDiscovery.ExistingChoice(pending, File.Exists);
                if (!String.IsNullOrWhiteSpace(pending) && pendingSelection == null)
                    throw new InvalidOperationException(text["gameSavedMissing"]);
                await bridge.Connect(store.Folder(store.Selected), c);
                status = await bridge.Status(c); ShowStatus();
                await bridge.Post("/api/language", new { language = text.Language }, c);
                if (discovery == null) await DiscoverGame(c);
                bool deferSteamChoice = pendingSelection?.Mode == "steam" && !SelectedPackageSupportsSteam();
                string chosen = deferSteamChoice ? null : GameDiscovery.PathToApply(pending ?? store.State.launcherPath, discovery, Json.Child(status, "game"), File.Exists, !String.IsNullOrWhiteSpace(pending));
                if (chosen != null)
                {
                    var selection = GameDiscovery.ExistingChoice(chosen, File.Exists);
                    if (selection == null) throw new InvalidOperationException(text["gameSavedMissing"]);
                    if (selection.Mode != "steam" || SelectedPackageSupportsSteam())
                    {
                        await bridge.Post("/api/settings", new { gameStart = selection.Mode, gameLauncher = selection.Path }, c);
                        status = await bridge.Status(c);
                    }
                }
                if (!String.IsNullOrWhiteSpace(pending) && !deferSteamChoice)
                {
                    var savedGame = Json.Child(status, "game");
                    if (Json.Text(savedGame, "mode") != pendingSelection.Mode || !String.Equals(Json.Text(savedGame, "launcher"), pendingSelection.Path, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException(text["gameChoiceUnconfirmed"]);
                    store.State.pendingLauncherPath = null;
                    try { store.Save(); } catch { store.State.pendingLauncherPath = pending; throw; }
                }
                connectionReady = true; connectionProblem = "";
                if (!riskRestored) { riskRestored = true; if (Json.Flag(status, "riskAcknowledged")) risk.IsChecked = true; }
                if (deferSteamChoice) OfferSteamPackageUpdate();
                ShowStatus();
            }
            catch (Exception e) { status = new Dictionary<string, object>(); connectionProblem = e.Message; ShowStatus(); throw; }
        }
        async Task Install(CancellationToken c)
        {
            if (compatible.IsChecked != true) throw new InvalidOperationException(text["compatRequired"]);
            // This checkbox explicitly combines version and visible account-risk
            // consent. Installation still never launches or acknowledges remotely.
            risk.IsChecked = true;
            await InstallRelease(SelectedRelease(), c);
        }
        async Task InstallRelease(Release release, CancellationToken c)
        {
            operationText.Text = text["preparingPackage"];
            var archive = await repo.Download(release, store.Cache, new Progress<double>(v => { progress.Value = v; operationText.Text = text["download"] + " " + (int)(v * 100) + "%"; }), c,
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "packages"));
            // Reject a mismatched release before stopping the working helper.
            // Full extraction/file verification still happens inside Install.
            await Task.Run(() => PackageStore.VerifyArchiveIdentity(archive, release, c), c);
            await PreparePackageChange(c);
            operationText.Text = text["verifying"]; progress.IsIndeterminate = true;
            await Task.Run(() => store.Install(archive, release, c), c);
            progress.IsIndeterminate = false; progress.Value = 1; operationText.Text = text["complete"];
            PopulateInstalled(); await Connect(c);
            details.Clear(); feedbackPanel.IsExpanded = false;
            operationText.Text = text["complete"];
        }
        // A version already on disk with the same identity (release, build and
        // archive checksum) is verified and selected, never downloaded again.
        async Task InstallOrSelect(Release release, CancellationToken c)
        {
            var present = store.State.installed.FirstOrDefault(i => i.release.id == release.id && i.release.buildId == release.buildId &&
                String.Equals(i.release.sha256, release.sha256, StringComparison.OrdinalIgnoreCase));
            if (present == null) { await InstallRelease(release, c); return; }
            await Task.Run(() => PackageStore.Verify(store.Folder(present), present.release, c), c);
            await PreparePackageChange(c); store.Select(present); PopulateInstalled(); await Connect(c);
            operationText.Text = String.Format(text["versionInstalled"], present.release.DisplayLabel);
        }
        Installed PreviousPackage()
        {
            return store.State.previous == store.State.selected ? null : store.State.installed.FirstOrDefault(i => i.folder == store.State.previous);
        }
        async Task UsePrevious(CancellationToken c)
        {
            var prior = PreviousPackage();
            if (prior == null) throw new InvalidOperationException(text["noPrevious"]);
            await Task.Run(() => PackageStore.Verify(store.Folder(prior), prior.release, c), c);
            await PreparePackageChange(c); store.Select(prior); PopulateInstalled(); await Connect(c);
            operationText.Text = String.Format(text["versionInstalled"], prior.release.DisplayLabel);
        }
        // Players should not have to notice that the VR package updates apart
        // from the app (Lelouche kept 1.1.x after updating the app, 13 Oct 2026).
        // On start, which includes the restart after an app update, a newer
        // public package for the same game version is installed while nothing
        // runs. The old one stays installed, one click away (step 02, Back to).
        // A version already installed is never reinstalled here: having it and
        // going back is a choice the player made.
        Release AutoPackageUpdate()
        {
            if (store.Selected == null || store.Selected.release.channel != "beta") return null;
            var newer = NewerPackage();
            if (newer == null || newer.gameVersion != store.Selected.release.gameVersion) return null;
            return store.State.installed.Any(i => i.release.id == newer.id) ? null : newer;
        }
        async Task AutoUpdatePackage()
        {
            var newer = AutoPackageUpdate();
            if (newer == null || preview || closing || operation != null || !connectionReady || bridge.Conflict != null || !autoPackageTried.Add(newer.id)) return;
            string from = store.Selected.release.DisplayLabel;
            await Run(async c =>
            {
                status = await bridge.Status(c);
                if (Json.Flag(status, "gameRunning") || Json.Flag(status, "injectorRunning") || Json.Flag(Json.Child(status, "job"), "running") ||
                    LauncherBridge.LaunchWorkerRunning(status)) { operationText.Text = text["packageUpdateLater"]; return; }
                await InstallRelease(newer, c);
                operationText.Text = String.Format(text["packageAutoUpdated"], newer.DisplayLabel, from);
            });
        }
        void Disconnected()
        {
            connectionReady = false;
            status = new Dictionary<string, object>();
            connectionProblem = text["connectionRequired"];
            ShowStatus();
        }
        async Task PreparePackageChange(CancellationToken c)
        {
            if (store.Selected == null) return;
            try { await bridge.PreparePackageChange(store.Folder(store.Selected), c); }
            finally { if (bridge.Address == null || bridge.Conflict != null) Disconnected(); }
        }
        void PopulateInstalled()
        {
            installed.Items.Clear(); foreach (var item in store.State.installed) installed.Items.Add(item); installed.SelectedItem = store.Selected;
            if (store.Selected != null)
            {
                var active = releases.Items.Cast<Release>().FirstOrDefault(r => r.id == store.Selected.release.id);
                if (active == null) { active = store.Selected.release; releases.Items.Add(active); }
                releases.SelectedItem = active;
            }
        }
        Release NewerPackage()
        {
            if (store.Selected == null) return null;
            DateTimeOffset installedDate;
            if (!DateTimeOffset.TryParse(store.Selected.release.published ?? store.Selected.release.created, out installedDate)) return null;
            return catalog.releases.Where(r => r.channel == "beta" && r.id != store.Selected.release.id)
                .Where(r => { DateTimeOffset date; return DateTimeOffset.TryParse(r.published, out date) && date > installedDate; })
                .OrderByDescending(r => DateTimeOffset.Parse(r.published)).FirstOrDefault();
        }
        async Task UpdateCatalog(CancellationToken c, bool quiet = false)
        {
            string chosen = SelectedRelease().id;
            try { catalog = RepoClient.KeepBundledCandidates(await repo.FetchCatalog(c), RepoClient.BundledCatalog()); catalogKey = "catalogOnline"; }
            catch (OperationCanceledException) when (quiet) { return; }
            catch (Exception e) when (!(e is OperationCanceledException)) { catalogKey = "catalogOffline"; if (!quiet) { details.Text = e.Message; lastNativeError = e.ToString(); } }
            if (closing) return;
            if (quiet) chosen = SelectedRelease().id;
            releases.Items.Clear(); foreach (var release in catalog.releases) releases.Items.Add(release);
            if (store.Selected != null && !releases.Items.Cast<Release>().Any(r => r.id == store.Selected.release.id)) releases.Items.Add(store.Selected.release);
            releases.SelectedItem = releases.Items.Cast<Release>().FirstOrDefault(r => r.id == chosen) ??
                releases.Items.Cast<Release>().FirstOrDefault(r => store.Selected != null && r.id == store.Selected.release.id) ?? (Release)releases.Items[0];
            catalogState.Text = text[catalogKey];
            if (!quiet) {
                operationText.Text = text[catalogKey];
                if (catalogKey == "catalogOffline") feedbackPanel.IsExpanded = true;
                OfferSteamPackageUpdate();
            }
            ShowStatus();
        }
        async Task CheckUpdates(CancellationToken c)
        {
            // Both feeds are independent; a catalog outage must not conceal a
            // launcher update, and a launcher outage must not block mod updates.
            await Task.WhenAll(UpdateCatalog(c), CheckLauncherUpdates(c, false));
        }
        async Task CheckLauncherUpdates(CancellationToken c, bool quiet)
        {
            if (preview || closing || !launcherUpdates.Installed) return;
            try { await launcherUpdates.CheckAsync(c); }
            catch (OperationCanceledException) { if (!quiet) throw; }
            catch (Exception error)
            {
                if (!quiet && !closing)
                {
                    details.Text = text["launcherUpdateOffline"]; lastNativeError = error.ToString();
                    feedbackPanel.IsExpanded = true;
                }
            }
            if (!closing) ShowStatus();
        }
        // The update bar downloads on the first click and asks before the restart.
        Task UpdateLauncher(CancellationToken c) { return RunLauncherUpdate(c, true); }
        // From the launch offer the player has already chosen to update: download and restart.
        Task UpdateLauncherNow(CancellationToken c) { return RunLauncherUpdate(c, false); }
        async Task RunLauncherUpdate(CancellationToken c, bool ask)
        {
            if (!launcherUpdates.ReadyToRestart)
            {
                operationText.Text = text["launcherDownloading"];
                IProgress<int> reporter = new Progress<int>(percent => {
                    if (!closing && operation != null && !c.IsCancellationRequested && !launcherUpdates.ReadyToRestart)
                        operationText.Text = String.Format(text["launcherDownloadProgress"], Math.Max(0, Math.Min(100, percent)));
                });
                await launcherUpdates.DownloadAsync(percent => reporter.Report(percent), c);
                operationText.Text = text["launcherReady"];
                ShowStatus();
                if (ask) return;
            }
            if (ask && !Confirm("launcherRestartConfirm")) return;
            // ApplyUpdatesAndRestart exits immediately. Persist settings first;
            // the service rechecks helper/native activity after the save.
            await launcherUpdates.RestartAsync(bridge.RequireLauncherUpdateIdle, store.Save, c);
        }
        void ShowLauncherUpdate()
        {
            string state = launcherUpdates.ReadyToRestart ? "launcherReady" : !launcherUpdates.Installed ? "launcherPortable" :
                launcherUpdates.CheckFailed ? "launcherUpdateOffline" : launcherUpdates.CheckedOnce ? "launcherCurrent" : "launcherUpdateHint";
            // While the update bar shows, the footer names only the version: the bar says the rest.
            launcherVersion.Text = String.Format(text["launcherVersion"], launcherUpdates.CurrentVersion) +
                (launcherUpdates.AvailableVersion != null ? "" : " · " + text[state]);
            launcherUpdateButton.Content = text[launcherUpdates.ReadyToRestart ? "restartLauncher" : "updateLauncher"];
            launcherUpdateButton.Visibility = launcherUpdates.AvailableVersion == null ? Visibility.Collapsed : Visibility.Visible;
            launcherUpdateBar.Visibility = launcherUpdateButton.Visibility;
            launcherUpdateBarText.Text = String.Format(text[launcherUpdates.ReadyToRestart ? "launcherUpdateBarReady" : "launcherUpdateBar"], launcherUpdates.AvailableVersion);
        }
        async Task Runtime(string mode, CancellationToken c)
        {
            if (!Confirm("runtimeConfirm")) return;
            await EnsureConnected(c); status = await bridge.Status(c);
            await SubmitJob("/api/runtime", new { mode = mode, expectedActive = Json.Text(Json.Child(status, "openxr"), "manifest") }, c);
        }
        async Task Remove(CancellationToken c)
        {
            var item = installed.SelectedItem as Installed;
            if (item == null) throw new InvalidOperationException(text["chooseVersion"]);
            if (!Confirm("removeConfirm")) return;
            await EnsureConnected(c); status = await bridge.Status(c);
            if (LauncherBridge.RemovalNeedsRestore(status))
                throw new InvalidOperationException(text["removeNeedsRestore"]);
            await bridge.Stop(c);
            Disconnected();
            await Task.Run(() => store.Remove(item), c); status.Clear(); PopulateInstalled(); operationText.Text = text["removed"];
            if (store.Selected != null) { await Connect(c); operationText.Text = text["removed"]; }
        }
        // Every version but the one in use, the previous one and any the active OpenXR runtime uses (OldVersions.cs).
        // The kept version stays connected: nothing it runs from is touched.
        async Task RemoveOld(CancellationToken c)
        {
            var plan = await Task.Run(() => OldVersions.Find(store, ActiveRuntimesOrNull()), c);
            if (plan.Empty) { operationText.Text = text["removeOldNone"]; return; }
            if (!Confirm("removeOldConfirm", plan.Versions.Count + plan.Leftovers.Count, SizeText(plan.Bytes), String.Join(", ", plan.Kept))) return;
            var result = await Task.Run(() => OldVersions.Remove(store, plan, c), c);
            PopulateInstalled();
            operationText.Text = String.Format(text[result.Retained.Count == 0 ? "removedOld" : "removedOldPartly"],
                result.Removed, SizeText(result.Freed), result.Retained.Count);
        }
        static string[] ActiveRuntimesOrNull()
        {
            try { return LauncherUninstall.ReadActiveRuntimes(); } catch (Exception) { return null; }
        }
        string SizeText(long bytes)
        {
            CultureInfo culture;
            try { culture = CultureInfo.GetCultureInfo(text.Language); } catch (CultureNotFoundException) { culture = CultureInfo.InvariantCulture; }
            return bytes >= 1L << 30 ? String.Format(text["sizeGB"], (bytes / (double)(1L << 30)).ToString("0.0", culture))
                : String.Format(text["sizeMB"], Math.Max(1L, bytes >> 20).ToString(culture));
        }
        Task Run(Func<CancellationToken, Task> action) { return RunAction(action, true); }
        async Task RunAction(Func<CancellationToken, Task> action, bool refreshAfter)
        {
            if (preview || operation != null) return;
            lastJobSnapshot = JobSnapshot(status);
            preserveActionFeedback = false; submittedJob = false;
            // A new attempt clears the last failure's red marks; Launch while installing is an install.
            string actionKey = pendingActionKey; pendingActionKey = null;
            int actionRow = actionKey == "launch" && WantsInstall() ? 1 : RowForAction(actionKey);
            failedRow = -2; failedMessage = "";
            var finished = operationFinished = new TaskCompletionSource<bool>();
            operation = new CancellationTokenSource(); foreach (var button in actions) button.IsEnabled = false; languages.IsEnabled = false; cancelButton.IsEnabled = true; cancelButton.Visibility = Visibility.Visible; progress.Visibility = Visibility.Visible;
            operationText.Text = text["busy"]; progress.IsIndeterminate = true;
            try
            {
                await action(operation.Token);
                preserveActionFeedback = !submittedJob && operationText.Text != text["busy"];
                if (!preserveActionFeedback && !submittedJob) operationText.Text = text["actionComplete"];
            }
            catch (OperationCanceledException) when (operation.IsCancellationRequested) { operationText.Text = text["cancelled"]; preserveActionFeedback = true; }
            catch (Exception e)
            {
                string message = e is OperationCanceledException ? text["requestInterrupted"] : e.Message;
                operationText.Text = text["failed"] + ": " + message; details.Text = message; feedbackPanel.IsExpanded = true; lastNativeError = e.ToString(); preserveActionFeedback = true;
                failedRow = actionRow; failedMessage = message;
                if (!connectionReady && !String.IsNullOrEmpty(connectionProblem)) ShowPage(false);
            }
            finally
            {
                bool cancelled = operation.IsCancellationRequested;
                operation.Dispose(); operation = null; progress.IsIndeterminate = false;
                foreach (var button in actions) button.IsEnabled = true; languages.IsEnabled = true;
                try { if (refreshAfter && !cancelled && !closeChecking) await RefreshStatus(); ShowStatus(); preserveActionFeedback = false; }
                finally { finished.TrySetResult(true); }
            }
        }
        async Task RefreshStatus()
        {
            if (closing || closeChecking || preview || polling || operation != null || bridge.Address == null) return;
            polling = true;
            try
            {
                // A successful poll is not a successful handover. Connection recovery
                // still has to validate the desired package and apply its saved choice.
                status = await bridge.Status(CancellationToken.None); ShowStatus();
            }
            catch (Exception e)
            {
                connectionReady = false; connectionProblem = e.Message;
                status = new Dictionary<string, object>();
                lastNativeError = e.ToString();
                if (!preserveActionFeedback) { operationText.Text = text["failed"] + ": " + e.Message; details.Text = e.Message; feedbackPanel.IsExpanded = true; }
                ShowStatus();
            }
            finally { polling = false; }
        }
        void ShowStatus()
        {
            if (rendering || gamePath == null) return;
            ShowLauncherUpdate();
            packageState.Text = store.Selected == null ? String.Format(text["versionSelected"], SelectedRelease().DisplayLabel) :
                String.Format(text["versionInstalled"], store.Selected.release.DisplayLabel) + (WantsInstall() ? Environment.NewLine +
                String.Format(text["versionSelected"], SelectedRelease().DisplayLabel) : "");
            packageState.ToolTip = (store.Selected == null ? "" : store.Selected.release.id + Environment.NewLine) + SelectedRelease().id;
            releases.ToolTip = SelectedRelease().id;
            var newerPackage = store.Selected == null ? null : NewerPackage();
            var previousPackage = store.Selected == null ? null : PreviousPackage();
            packageUpdateButton.Visibility = newerPackage == null ? Visibility.Collapsed : Visibility.Visible;
            if (newerPackage != null) packageUpdateButton.Content = String.Format(text["updatePackageTo"], newerPackage.DisplayLabel);
            packageRollbackButton.Visibility = previousPackage == null ? Visibility.Collapsed : Visibility.Visible;
            if (previousPackage != null) packageRollbackButton.Content = String.Format(text["rollbackTo"], previousPackage.release.DisplayLabel);
            var game = Json.Child(status, "game"); string launcher = Json.Text(game, "launcher");
            bool pendingChoice = !String.IsNullOrWhiteSpace(store.State.pendingLauncherPath);
            gamePath.Text = pendingChoice ? store.State.pendingLauncherPath : Json.Flag(game, "saved") && Json.Text(game, "mode") == "manual" ? text["manualStart"] : String.IsNullOrEmpty(launcher) ? (discovery == null ? store.State.launcherPath : discovery.Path) : launcher;
            if (String.IsNullOrEmpty(gamePath.Text)) gamePath.Text = text["noGame"];
            gamePath.ToolTip = gamePath.Text;
            string hint = previewHint ?? (discovery == null ? (preview ? "locateHint" : "findingGame") : discovery.Message);
            if (SteamChoiceNeedsPackage()) hint = "gameSteamUpdateRequired";
            if (!pendingChoice && Json.Flag(game, "saved")) hint = Json.Text(game, "mode") == "manual" ? "gameManualSaved" :
                String.IsNullOrEmpty(Json.Text(game, "problem")) ? Json.Text(game, "mode") == "steam" ? "gameSteamSaved" : "gameSavedFound" : "gameSavedMissing";
            gameHint.Text = text[hint];
            // These hints say what is broken; the row gets a red "!" and the hint turns red.
            bool gameProblem = new[] { "gameSavedMissing", "gameNotFound", "gameSettingsUnreadable", "gameSteamUpdateRequired" }.Contains(hint);
            gameHint.Foreground = gameProblem ? danger : muted;
            gameHint.ToolTip = discovery != null && discovery.Candidates.Count > 1 ? String.Join(Environment.NewLine, discovery.Candidates) : gameHint.Text;
            runtime.Text = store.Selected != null && !connectionReady ? text["runtimeNotChecked"] : LauncherPresentation.RuntimeSummary(bridge.Address != null || previewConnected, status, k => text[k]);
            runtimeCheck.IsEnabled = operation == null && store.Selected != null;
            var job = Json.Child(status, "job"); var launch = Json.Child(status, "launch");
            bool workerRunning = LauncherBridge.LaunchWorkerRunning(status), running = Json.Flag(job, "running") || workerRunning;
            bool workerStalled = workerRunning && (Json.Flag(launch, "stalled") || Json.Flag(launch, "activityUnknown"));
            // A connection loss clears status for display but cannot prove that its
            // previously observed startup worker stopped. Resolve only from fresh status.
            if (connectionReady && status.ContainsKey("job")) unresolvedStartup = workerRunning || (Json.Flag(job, "running") && Json.Text(job, "kind") == "launch");
            string phase = LauncherBridge.CurrentLaunch(status) ? Json.Text(launch, "phase") : "";
            if (phase != shownPhase) { shownPhase = phase; phaseSince = DateTime.UtcNow; }
            string phaseKey = workerRunning && !workerStalled ?
                LauncherPresentation.LaunchPhaseKey(phase, Json.Text(game, "mode") == "steam", DateTime.UtcNow - phaseSince > SlowVrStart) : null;
            bool inVr = LauncherPresentation.InVr(status);
            bool launchJob = Json.Flag(job, "running") && Json.Text(job, "kind") == "launch";
            bool workerFailed = LauncherBridge.CurrentLaunch(status) && Json.Text(launch, "phase") == "failed";
            bool workerCancelled = LauncherBridge.CurrentLaunch(status) && Json.Text(launch, "phase") == "cancelled";
            bool hasPackage = store.Selected != null, recording = Json.Flag(Json.Child(status, "recording"), "running");
            bool conflict = bridge.Conflict != null;
            connectionPanel.Visibility = hasPackage && !connectionReady && operation == null ? Visibility.Visible : Visibility.Collapsed;
            connectionMessage.Text = conflict ? text["launcherConflict"] + Environment.NewLine + bridge.Conflict.AppRoot : text["connectionRetryInfo"] + Environment.NewLine + connectionProblem;
            bool gameRunning = Json.Flag(status, "gameRunning"), injectorRunning = Json.Flag(status, "injectorRunning");
            var xr = Json.Child(status, "openxr"); bool simulator = Json.Flag(xr, "isSimulator");
            string runtimeBlock = LauncherPresentation.RuntimeLaunchBlock(connectionReady, status);
            bool wantsInstall = WantsInstall();
            launchButton.Content = text[wantsInstall ? hasPackage ? "installSelected" : "install" : "launch"];
            versionConsent.Visibility = wantsInstall ? Visibility.Visible : Visibility.Collapsed;
            risk.Visibility = wantsInstall ? Visibility.Collapsed : Visibility.Visible;
            gameVersion.Text = text["targetGame"] + " " + (wantsInstall ? SelectedRelease().gameVersion : store.Selected.release.gameVersion);
            runtimeHint.Text = text[!hasPackage ? "runtimeInstallHint" : !connectionReady ? "connectionRequired" : gameRunning || injectorRunning ? "runtimeCloseFirst" : runtimeBlock ?? (Json.Flag(xr, "canSimulator") && !Json.Flag(xr, "canHeadset") ? "runtimeSimulatorOnlyHint" : "runtimeChoiceHint")];
            // A launch that failed before the worker began leaves only its job's error.
            bool launchJobFailed = Json.Text(job, "kind") == "launch" && Json.Flag(job, "error") && !Json.Flag(job, "running") && !workerFailed && !workerCancelled;
            bool launchFailed = workerFailed || launchJobFailed || failedRow == -1;
            string failureKey = workerFailed || workerCancelled ? LauncherPresentation.FailureKey(launch, workerCancelled) : launchFailed ? "failUnknown" : workerStalled ? "failStalled" : null;
            launchState.Foreground = launchFailed || workerStalled ? danger : muted;
            launchState.Text = phaseKey != null ? text[phaseKey] : failureKey != null ? text[failureKey] : gameRunning || running ? LauncherBridge.LaunchSummary(status, k => text[k]) :
                text[wantsInstall ? "installNext" : SteamChoiceNeedsPackage() ? "gameSteamUpdateRequired" : !connectionReady ? "connectionRequired" : injectorRunning ? "runtimeCloseFirst" : runtimeBlock ?? (risk.IsChecked != true ? "riskNext" : Json.Text(game, "mode") == "steam" ? "launchNextSteam" : "launchNext")];
            feedbackPanel.Visibility = String.IsNullOrWhiteSpace(details.Text) ? Visibility.Collapsed : Visibility.Visible;
            // Done steps get a check; the first unfinished one is outlined.
            bool gameReady = pendingChoice || (gamePath.Text != text["noGame"] && !new[] { "gameSavedMissing", "gameNotFound",
                "gameSettingsUnreadable", "gameMultiple", "findingGame", "locateHint" }.Contains(hint));
            bool packageReady = hasPackage && !wantsInstall, runtimeReady = packageReady && connectionReady && runtimeBlock == null;
            var done = new[] { gameReady, packageReady, runtimeReady };
            // A broken row (red "!") outranks done or to-do. Its reason goes under it, unless the
            // row's own line already says it (the game hint).
            var problems = new string[3];
            if (gameProblem) problems[0] = "";
            if (hasPackage && !connectionReady && operation == null) problems[1] = text["connectionRequired"];
            if (connectionReady && (runtimeBlock == "runtimeUnavailableHint" || runtimeBlock == "runtimeSelectCurrentSimulator")) problems[2] = text[runtimeBlock];
            int jobRow = Json.Flag(job, "error") && !Json.Flag(job, "running") ? RowForJob(Json.Text(job, "kind")) : -2;
            if (jobRow >= 0) problems[jobRow] = text["failed"] + ": " + LastLine(Json.Text(job, "output"));
            if (failedRow >= 0) problems[failedRow] = failedMessage;
            int currentStep = Array.IndexOf(done, false);
            for (int index = 0; index < done.Length; ++index)
                ShowStep(index, problems[index] != null ? StepState.Problem : done[index] ? StepState.Done : index == currentStep ? StepState.Current : StepState.Pending, problems[index]);
            bool anyProblem = problems.Any(p => p != null);
            // Nothing is running: the hero points at the marked row instead of the next launch step.
            if (anyProblem && failureKey == null && phaseKey == null && !gameRunning && !running && !wantsInstall) launchState.Text = text["problemNext"];
            bool stereoSeen = LauncherBridge.LaunchSummary(status, k => k) == "stereoSeen";
            heroTitle.Text = text[launchFailed ? "heroFailed" : workerCancelled ? "heroCancelled" : workerStalled ? "heroStalled" :
                inVr || stereoSeen ? "heroInVr" : workerRunning || launchJob ? "heroStarting" :
                gameRunning ? "heroGameRunning" : anyProblem ? "heroProblem" : wantsInstall ? "heroInstall" :
                gameReady && connectionReady && runtimeBlock == null && !SteamChoiceNeedsPackage() ? "heroReady" : "heroSetup"];
            heroCard.BorderBrush = launchFailed || workerStalled ? danger : anyProblem ? dangerLine : line;
            heroHelp.Visibility = launchFailed ? Visibility.Visible : Visibility.Collapsed;
            // The chips stay after a failed or cancelled launch, showing the step it stopped at.
            int stoppedStep = workerFailed || workerCancelled ? LauncherPresentation.StoppedStep(launch) : launchFailed ? 0 : -1;
            int litStep = workerRunning || launchJob ? LauncherPresentation.LaunchStep(phase) : stoppedStep >= 0 ? stoppedStep : -2;
            bool litBad = workerStalled || (launchFailed && !(workerRunning || launchJob)), litStopped = workerCancelled && !(workerRunning || launchJob);
            phaseRow.Visibility = litStep == -2 ? Visibility.Collapsed : Visibility.Visible;
            for (int i = 0; i < phaseChips.Length; ++i)
            {
                bool lit = i == litStep;
                phaseChips[i].Text = (i < litStep ? "✓ " : lit && litBad ? "! " : lit && litStopped ? "– " : "") + text[PhaseChipKeys[i]];
                phaseChips[i].Foreground = lit && !litStopped ? background : i < litStep || lit ? foreground : muted;
                ((Border)phaseChips[i].Parent).Background = lit ? litBad ? danger : litStopped ? line : accent : badge;
            }
            // The account risk shows until it is accepted once (the helper remembers that), and
            // always while installing, where it is part of the version consent.
            consentNotice.Visibility = wantsInstall || (connectionReady && !Json.Flag(status, "riskAcknowledged")) ? Visibility.Visible : Visibility.Collapsed;
            playFlatButton.Visibility = !hasPackage || wantsInstall || Json.Text(game, "mode") == "manual" ? Visibility.Collapsed : Visibility.Visible;
            var capture = Json.Child(status, "recording");
            ShowMicrophones(capture);
            bool canRecord = CaptureAvailability.HasSource(status, new[] { "auto", "steamvr", "simulator" }[Math.Max(0, source.SelectedIndex)]);
            recordingState.Text = text[!hasPackage ? "chooseVersion" : !connectionReady ? "connectionRequired" : recording ? "recordingActive" : !gameRunning ? "recordNeedsGame" : !canRecord ? "recordUnavailable" : "recordReady"];
            foreach (var button in actions)
            {
                string key = button.Tag as string, reason = null;
                if (key == "controllerRefresh" || key == "controllerCopy") continue;
                if (key == "troubleshooting" || key == "backToSetup" || key == "processRecoveryOpen") { button.IsEnabled = !closing; continue; }
                if (key == "useHeadset" || key == "useSimulator")
                {
                    bool current = connectionReady && (key == "useHeadset" ? !simulator && Json.Flag(xr, "canHeadset") : Json.Flag(xr, "isBundledSimulator"));
                    button.Content = text[key] + (current ? " · " + text["current"] : "");
                    button.BorderBrush = current ? accent : Brushes.Transparent;
                }
                if (operation != null) reason = "busy";
                else
                {
                    switch (key)
                    {
                        case "processRecoveryScan": case "prepareUninstall": break; // Independent of helper connection/job state; cleanup verifies activity itself.
                        case "processRecoveryStop": reason = processRecoveryFresh && processRecoveryReport?.Candidates != null &&
                            processRecoveryReport.Candidates.Any(item => item.Eligible && processRecoverySelected.Contains(item.Pid)) ? null : "processRecoveryChoose"; break;
                        case "useGameLocation": reason = running ? "busy" : gameLocations.SelectedItem == null ? "gameChooseHint" : null; break;
                        case "browse": case "browseGameFile": reason = running ? "busy" : null; break;
                        case "install": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : compatible.IsChecked != true ? "compatRequired" : null; break;
                        case "updatePackage": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : NewerPackage() == null ? "noPackageUpdate" : null; break;
                        case "installVersion": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" :
                            store.Selected != null && SelectedRelease().id == store.Selected.release.id ? "alreadySelected" : null; break;
                        case "updateLauncher": reason = launcherUpdates.Busy ? "busy" : launcherUpdates.AvailableVersion == null ? "launcherUpdateHint" :
                            launcherUpdates.ReadyToRestart && (running || recording || gameRunning || injectorRunning) ? "launcherRestartIdle" : null; break;
                        case "playFlat": reason = running ? "busy" : gameRunning ? "alreadyRunning" : injectorRunning ? "runtimeCloseFirst" : !gameReady ? "locateHint" : null; break;
                        case "launch": reason = running ? "busy" : gameRunning ? "alreadyRunning" : injectorRunning ? "runtimeCloseFirst" :
                            gameProblem && hint != "gameSteamUpdateRequired" && !wantsInstall ? hint : wantsInstall ? (conflict ? "connectionRequired" : compatible.IsChecked != true ? "compatRequired" : null) : SteamChoiceNeedsPackage() ? "gameSteamUpdateRequired" : !connectionReady ? "connectionRequired" : runtimeBlock ?? (risk.IsChecked != true ? "riskAccept" : null); break;
                        case "recordStart": reason = !hasPackage ? "chooseVersion" : running || recording ? "busy" : !gameRunning ? "recordNeedsGame" : !canRecord ? "recordUnavailable" : null; break;
                        case "recordStop": reason = !recording ? "noRecording" : null; break;
                        case "repair": case "useVersion": case "remove": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : installed.SelectedItem == null ? "chooseVersion" : null; break;
                        case "rollback": case "rollbackPackage": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : PreviousPackage() == null ? "noPrevious" : null; break;
                        case "useHeadset": case "useSimulator":
                            reason = !hasPackage ? "chooseVersion" : running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" :
                                !Json.Flag(xr, key == "useHeadset" ? "canHeadset" : "canSimulator") ? "runtimeUnavailableHint" :
                                (key == "useHeadset" ? !simulator : Json.Flag(xr, "isBundledSimulator")) ? "alreadySelected" : null; break;
                        case "restore": case "reset": reason = !hasPackage ? "chooseVersion" : running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : null; break;
                        case "check": case "connect": case "web": case "openRecordings":
                            reason = !hasPackage ? "chooseVersion" : running ? "busy" : null; break;
                        case "disconnect": reason = bridge.Address == null ? "helperStopped" : running ? "busy" : null; break;
                        case "openExisting": case "switchLauncher": reason = conflict ? null : "connectionRequired"; break;
                        case "retryConnection": reason = hasPackage ? null : "chooseVersion"; break;
                    }
                    if (hasPackage && !connectionReady && new[] { "recordStart", "recordStop", "remove", "useHeadset", "useSimulator", "restore", "reset", "check", "web", "openRecordings" }.Contains(key)) reason = "connectionRequired";
                    if (conflict && new[] { "install", "repair", "useVersion", "rollback", "rollbackPackage", "updatePackage", "installVersion" }.Contains(key)) reason = "connectionRequired";
                }
                button.IsEnabled = reason == null;
                button.ToolTip = reason == null ? null : text[reason];
                if (key == "openExisting" || key == "switchLauncher") button.Visibility = conflict ? Visibility.Visible : Visibility.Collapsed;
            }
            UpdateControllerCheck();
            UpdateProcessRecovery();
            foreach (var choice in new[] { releases, installed, source, fps, size, purpose, gameLocations }) choice.IsEnabled = operation == null && !running;
            if (recordSound != null) recordSound.IsEnabled = operation == null && !running;
            if (recordMic != null) { recordMic.IsEnabled = operation == null && !running; recordMicVolume.IsEnabled = recordMic.IsEnabled && recordMic.SelectedIndex > 0; }
            if (recordSoundFiles != null) recordSoundFiles.IsEnabled = operation == null && !running;
            bool active = operation != null || running;
            // Only when something looks stuck: not during every healthy launch.
            recoveryShortcut.Visibility = workerStalled || (unresolvedStartup && !workerRunning && !launchJob) || (hasPackage && !connectionReady) ? Visibility.Visible : Visibility.Collapsed;
            // In VR there is nothing left to wait for or stop: the worker only finishes its check.
            bool canCancel = operation != null || (bridge.Address != null && LauncherBridge.CanCancelLaunch(status) && !inVr);
            cancelButton.Content = text[operation == null && canCancel ? "stopWaiting" : "cancel"];
            cancelButton.IsEnabled = canCancel; cancelButton.Visibility = canCancel ? Visibility.Visible : Visibility.Collapsed;
            progress.Visibility = active && !(operation == null && (workerStalled || inVr)) ? Visibility.Visible : Visibility.Collapsed;
            if (operation == null)
            {
                progress.IsIndeterminate = running && !workerStalled && !inVr;
                string snapshot = JobSnapshot(status);
                if (snapshot != lastJobSnapshot)
                {
                    // Consume each update once, including updates hidden by newer local feedback.
                    lastJobSnapshot = snapshot;
                    if (!preserveActionFeedback)
                    {
                        if (running)
                        {
                            // A known phase is told in the hero; its raw message stays in Details.
                            operationText.Text = phaseKey != null ? "" : workerRunning && !String.IsNullOrWhiteSpace(Json.Text(launch, "message")) ? Json.Text(launch, "message") : Json.Text(job, "message");
                            if (workerRunning) { details.Text = StartupDetails(launch); feedbackPanel.Visibility = Visibility.Visible; if (workerStalled) feedbackPanel.IsExpanded = true; }
                        }
                        else if (workerFailed || workerCancelled)
                        {
                            string message = Json.Text(launch, "message");
                            details.Text = String.IsNullOrWhiteSpace(message) ? text[workerFailed ? "launchFailed" : "launchCancelled"] : message;
                            operationText.Text = "";
                            feedbackPanel.Visibility = Visibility.Visible; feedbackPanel.IsExpanded = workerFailed;
                        }
                        else if (Json.Flag(job, "error") || !String.IsNullOrEmpty(Json.Text(job, "code")))
                        {
                            string kind = Json.Text(job, "kind");
                            string output = Json.Text(job, "output").Trim();
                            string summary = LastLine(output);
                            operationText.Text = text[Json.Flag(job, "error") ? "failed" : kind == "check" ? "checkComplete" : kind == "runtime" ? "runtimeComplete" : "actionComplete"] +
                                (String.IsNullOrEmpty(summary) ? "" : " " + summary);
                            details.Text = Json.Text(job, "output");
                            feedbackPanel.Visibility = String.IsNullOrWhiteSpace(details.Text) ? Visibility.Collapsed : Visibility.Visible;
                            feedbackPanel.IsExpanded = Json.Flag(job, "error") || kind == "check";
                        }
                    }
                }
            }
            // Red for a failure, whichever path wrote the line (an action, a helper job, the launch).
            operationText.Foreground = operationText.Text.StartsWith(text["failed"]) || operationText.Text.StartsWith(text["launchFailed"]) ? danger : muted;
        }
        static string LastLine(string output)
        {
            string last = ((output ?? "").Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).LastOrDefault() ?? "").Trim();
            return last.Length > 180 ? last.Substring(0, 177) + "…" : last;
        }
        // Documentation renders: preview-state\preview.json may supply observed helper
        // status so screenshots show a ready launcher without a helper, game or headset.
        public void ApplyPreviewFixture(Dictionary<string, object> fixture)
        {
            if (!preview) throw new InvalidOperationException("Preview fixtures apply only to --preview renders.");
            status = Json.Child(fixture, "status");
            connectionReady = previewConnected = Json.Flag(fixture, "connected");
            risk.IsChecked = Json.Flag(fixture, "riskAccepted");
            compatible.IsChecked = Json.Flag(fixture, "versionConfirmed");
            operationText.Text = Json.Text(fixture, "operation");
            // Unhappy-path renders: a game hint, a failed action under its row, the details.
            previewHint = String.IsNullOrEmpty(Json.Text(fixture, "gameHint")) ? null : Json.Text(fixture, "gameHint");
            if (fixture.ContainsKey("failedRow")) { failedRow = int.Parse(Json.Text(fixture, "failedRow")); failedMessage = Json.Text(fixture, "failedMessage"); }
            if (!String.IsNullOrEmpty(Json.Text(fixture, "details"))) { details.Text = Json.Text(fixture, "details"); feedbackPanel.IsExpanded = true; }
            if (Json.Flag(fixture, "troubleshooting")) ShowPage(true);
            if (Json.Flag(fixture, "moreOptions")) { morePanel.IsExpanded = true; advancedPanel.IsExpanded = true; }
            ShowStatus();
        }
        public void SavePreview(string path, double scale = 1)
        {
            var visual = (FrameworkElement)Content;
            visual.Measure(new Size(780, 700)); visual.Arrange(new Rect(0, 0, 780, 700)); visual.UpdateLayout(); FitBackdrop(); visual.UpdateLayout();
            var image = new RenderTargetBitmap((int)Math.Round(780 * scale), (int)Math.Round(700 * scale), 96 * scale, 96 * scale, PixelFormats.Pbgra32); image.Render(visual);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            using (var stream = File.Create(path)) encoder.Save(stream);
        }
    }
}
