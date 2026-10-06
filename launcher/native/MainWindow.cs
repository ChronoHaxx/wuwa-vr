using System;
using System.Collections.Generic;
using System.Diagnostics;
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
        bool connectionReady;
        string connectionProblem = "";
        string lastJobSnapshot, lastNativeError = "";
        bool allowClose, closeChecking, unresolvedStartup;
        Func<string, bool> closeOnlyConfirmOverride;
        TimeSpan closeTimeout = TimeSpan.FromSeconds(6);
        ComboBox languages, releases, installed, source, fps, size, purpose;
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
        TextBlock packageUpdateState;
        Button packageUpdateButton;
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
        CheckBox compatible, risk;
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
        readonly Brush background = Color("#10191D"), card = Color("#19262C"), foreground = Color("#EDF3F1"), muted = Color("#A5B8BE"), teal = Color("#82DFC7");
        public MainWindow(PackageStore store, string backendData, bool preview = false, string language = null,
            LauncherUpdateService launcherUpdates = null)
        {
            this.store = store; this.preview = preview; bridge = new LauncherBridge(backendData);
            this.launcherUpdates = preview ? new LauncherUpdateService() : launcherUpdates ?? new LauncherUpdateService();
            text.Language = language ?? store.State.language;
            Title = "WuWa VR"; Width = 820; Height = 770; MinWidth = 720; MinHeight = 680;
            Background = background; Foreground = foreground; FontFamily = new FontFamily("Segoe UI"); FontSize = 14;
            Resources.Add(typeof(Button), ButtonStyle());
            Resources.Add(typeof(ComboBox), ChoiceStyle());
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
                poll.Start();
                await Task.WhenAll(CheckLauncherUpdates(updateLifetime.Token, true), UpdateCatalog(updateLifetime.Token, true));
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
            if (stopped || cancelled) return stopped;
            details.Text = remaining; feedbackPanel.Visibility = Visibility.Visible; feedbackPanel.IsExpanded = true;
            OpenProcessRecovery();
            string prompt = text["closeOnlyPrompt"] + Environment.NewLine + Environment.NewLine + remaining;
            return closeOnlyConfirmOverride != null ? closeOnlyConfirmOverride(prompt) :
                MessageBox.Show(this, prompt, text["closeOnlyTitle"], MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) == MessageBoxResult.Yes;
        }
        void OpenProcessRecovery()
        {
            ShowPage(true); processRecoveryPanel.IsExpanded = true;
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
            style.Setters.Add(new Setter(Control.BackgroundProperty, Color("#25383F")));
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
            hover.Setters.Add(new Setter(OpacityProperty, 0.85)); style.Triggers.Add(hover);
            var focused = new Trigger { Property = IsKeyboardFocusedProperty, Value = true };
            focused.Setters.Add(new Setter(Control.BorderBrushProperty, teal));
            focused.Setters.Add(new Setter(Control.BorderThicknessProperty, new Thickness(2))); style.Triggers.Add(focused);
            var disabled = new Trigger { Property = IsEnabledProperty, Value = false };
            disabled.Setters.Add(new Setter(OpacityProperty, 0.4));
            disabled.Setters.Add(new Setter(CursorProperty, System.Windows.Input.Cursors.Arrow)); style.Triggers.Add(disabled);
            return style;
        }
        TextBlock Label(string value, double fontSize = 14, Brush brush = null)
        { return new TextBlock { Text = value, FontSize = fontSize, Foreground = brush ?? foreground, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 4) }; }
        Button Action(string key, Func<CancellationToken, Task> fn, bool primary = false)
        {
            var button = new Button { Content = text[key], Padding = new Thickness(14, 9, 14, 9), Margin = new Thickness(0, 4, 8, 4),
                Background = primary ? teal : Color("#25383F"), Foreground = primary ? background : foreground, Cursor = System.Windows.Input.Cursors.Hand, Tag = key };
            ToolTipService.SetShowOnDisabled(button, true);
            button.Click += async (s, e) => await Run(async c =>
            {
                await fn(c);
                if (!submittedJob && operationText.Text == text["busy"])
                    operationText.Text = text[key] + " · " + text["actionComplete"];
            }); actions.Add(button); return button;
        }
        Button Link(string key, string url)
        { return Action(key, c => { Open(url); operationText.Text = text["browserOpened"]; return Task.CompletedTask; }); }
        string guideDirectoryOverride = null;
        string GuideUrl(string page)
        {
            // Resolve on click: install/rollback and language can change while
            // the window stays open. A broken helper must not hide offline help.
            if (page == "guide.html")
            {
                var launcherGuide = Path.Combine(guideDirectoryOverride ?? AppDomain.CurrentDomain.BaseDirectory, "PlayerGuide.html");
                if (File.Exists(launcherGuide)) return new Uri(launcherGuide).AbsoluteUri + (text.Language == "zh-Hans" ? "#zh" : "#en");
            }
            if (store.Selected != null)
            {
                var parts = page.Split(new[] { '#' }, 2);
                string languageRoot = text.Language == "zh-Hans" ? "l/zh-Hans/" : "";
                // Candidate instructions describe this desktop app; the full
                // published guide remains a linked portable reference.
                if (parts[0] == "guide.html" && File.Exists(Paths.Inside(store.Folder(store.Selected), "app/site/" + languageRoot + "quickstart.html")))
                    parts[0] = "quickstart.html";
                string relative = languageRoot + parts[0];
                var file = Paths.Inside(store.Folder(store.Selected), "app/site/" + relative);
                if (File.Exists(file)) return new Uri(file).AbsoluteUri + (parts.Length == 2 ? "#" + parts[1] : "");
            }
            return "https://chronohaxx.github.io/wuwa-vr/" + page;
        }
        Button GuideLink(string key, string page, bool footer = false)
        {
            var link = Action(key, c =>
            {
                var url = GuideUrl(page); Open(url);
                operationText.Text = text[text.Language == "zh-Hans" && !new Uri(url).IsFile ? "englishGuideFallback" : "browserOpened"];
                return Task.CompletedTask;
            });
            if (footer) { link.Background = Brushes.Transparent; link.Foreground = muted; link.FontSize = 11;
                link.Padding = new Thickness(4, 3, 4, 3); link.Margin = new Thickness(0, 0, 12, 0); }
            return link;
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
            var panel = new StackPanel(); panel.Children.Add(Label(title, 18));
            foreach (var e in elements) panel.Children.Add(e);
            return new Border { Background = card, CornerRadius = new CornerRadius(10), Padding = new Thickness(20, 14, 20, 14), Margin = new Thickness(0, 0, 0, 12), Child = panel };
        }
        Style ChoiceStyle()
        {
            // Keep the ComboBox's native keyboard/selection behavior while styling its popup too.
            return (Style)System.Windows.Markup.XamlReader.Parse(@"
<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='{x:Type ComboBox}'>
 <Setter Property='Foreground' Value='#EDF3F1'/><Setter Property='Background' Value='#223239'/><Setter Property='BorderBrush' Value='#3B5159'/>
 <Setter Property='ScrollViewer.HorizontalScrollBarVisibility' Value='Disabled'/>
 <Setter Property='ItemContainerStyle'><Setter.Value><Style TargetType='{x:Type ComboBoxItem}'>
  <Setter Property='Foreground' Value='#EDF3F1'/><Setter Property='Background' Value='#19262C'/><Setter Property='Padding' Value='10,8'/>
  <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type ComboBoxItem}'><Border Background='{TemplateBinding Background}' Padding='{TemplateBinding Padding}'><ContentPresenter/></Border></ControlTemplate></Setter.Value></Setter>
  <Style.Triggers><Trigger Property='IsHighlighted' Value='True'><Setter Property='Background' Value='#36594F'/></Trigger><Trigger Property='IsSelected' Value='True'><Setter Property='Background' Value='#315047'/></Trigger></Style.Triggers>
 </Style></Setter.Value></Setter>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='{x:Type ComboBox}'>
  <Grid>
   <Border x:Name='Shell' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='1' CornerRadius='6'/>
   <ToggleButton Focusable='False' IsChecked='{Binding IsDropDownOpen, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}' Background='Transparent'>
    <ToggleButton.Template><ControlTemplate TargetType='{x:Type ToggleButton}'><Border Background='{TemplateBinding Background}'/></ControlTemplate></ToggleButton.Template>
   </ToggleButton>
   <ContentPresenter Margin='11,8,30,8' IsHitTestVisible='False' VerticalAlignment='Center' Content='{TemplateBinding SelectionBoxItem}' ContentTemplate='{TemplateBinding SelectionBoxItemTemplate}' ContentTemplateSelector='{TemplateBinding ItemTemplateSelector}'/>
   <TextBlock Text='⌄' Margin='0,0,11,3' Foreground='#A5B8BE' HorizontalAlignment='Right' VerticalAlignment='Center' IsHitTestVisible='False'/>
   <Popup x:Name='PART_Popup' Placement='Bottom' IsOpen='{TemplateBinding IsDropDownOpen}' AllowsTransparency='True' Focusable='False' PopupAnimation='Fade'>
    <Border Background='#19262C' BorderBrush='#526A72' BorderThickness='1' CornerRadius='6' MinWidth='{Binding ActualWidth, RelativeSource={RelativeSource TemplatedParent}}' MaxHeight='280' Padding='3' Margin='0,3,0,0'>
     <ScrollViewer CanContentScroll='True' HorizontalScrollBarVisibility='Disabled'><ItemsPresenter KeyboardNavigation.DirectionalNavigation='Contained'/></ScrollViewer>
    </Border>
   </Popup>
  </Grid>
  <ControlTemplate.Triggers><Trigger Property='IsKeyboardFocusWithin' Value='True'><Setter TargetName='Shell' Property='BorderBrush' Value='#82DFC7'/></Trigger><Trigger Property='IsMouseOver' Value='True'><Setter TargetName='Shell' Property='BorderBrush' Value='#82DFC7'/></Trigger><Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.45'/></Trigger></ControlTemplate.Triggers>
 </ControlTemplate></Setter.Value></Setter>
</Style>");
        }
        ComboBox Choice(params string[] items)
        {
            var control = new ComboBox { Margin = new Thickness(0, 4, 0, 4), MinWidth = 130, MaxDropDownHeight = 280 };
            foreach (string item in items) control.Items.Add(item); control.SelectedIndex = 0; return control;
        }
        Border Step(string number, string title, Button action, params UIElement[] elements)
        {
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(43) });
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var numberText = Label(number, 13, teal); numberText.HorizontalAlignment = HorizontalAlignment.Center; numberText.VerticalAlignment = VerticalAlignment.Center;
            var badge = new Border { Width = 30, Height = 30, CornerRadius = new CornerRadius(8), Background = Color("#263F3C"), Child = numberText, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(0, 2, 12, 0) };
            grid.Children.Add(badge);
            var body = new StackPanel { Margin = new Thickness(0, 0, 16, 0) };
            var heading = Label(title, 16); heading.FontWeight = FontWeights.SemiBold; body.Children.Add(heading);
            foreach (var element in elements) body.Children.Add(element);
            Grid.SetColumn(body, 1); grid.Children.Add(body);
            if (action != null) { action.Margin = new Thickness(0); action.VerticalAlignment = VerticalAlignment.Center; Grid.SetColumn(action, 2); grid.Children.Add(action); }
            return new Border { Background = card, BorderBrush = Color("#293C43"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(9), Padding = new Thickness(16, 11, 16, 11), Margin = new Thickness(0, 0, 0, 9), Child = grid };
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
                uninstallOpen = uninstallPanel?.IsExpanded == true;
            string savedDetails = details?.Text, savedFeedback = operationText?.Text;
            rendering = true; actions.Clear();
            var outer = new DockPanel { Margin = new Thickness(24, 16, 24, 12), Background = background };
            var header = new Grid { Margin = new Thickness(0, 0, 0, 14) };
            System.Windows.Input.KeyboardNavigation.SetTabIndex(header, 0);
            header.ColumnDefinitions.Add(new ColumnDefinition()); header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var heading = new StackPanel();
            var title = Label(text["title"], 26); title.FontWeight = FontWeights.SemiBold; heading.Children.Add(title);
            heading.Children.Add(Label(text["beta"], 10, teal));
            header.Children.Add(heading);
            languages = Choice("English", "简体中文"); languages.SelectedIndex = text.Language == "zh-Hans" ? 1 : 0;
            languages.SelectionChanged += (s, e) =>
            {
                if (rendering || operation != null) return;
                text.Language = languages.SelectedIndex == 1 ? "zh-Hans" : "en";
                if (!preview) { store.State.language = text.Language; store.Save(); }
                Render();
            };
            var languagePanel = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            languagePanel.Children.Add(Label(text["language"], 12, muted)); languagePanel.Children.Add(languages); Grid.SetColumn(languagePanel, 1); header.Children.Add(languagePanel);
            DockPanel.SetDock(header, Dock.Top); outer.Children.Add(header);
            var footer = new StackPanel { Margin = new Thickness(0, 12, 0, 0) };
            System.Windows.Input.KeyboardNavigation.SetTabIndex(footer, 2);
            footer.Children.Add(new Border { Height = 1, Background = Color("#293C43"), Margin = new Thickness(0, 0, 0, 9) });
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
                await bridge.Post("/api/settings", new { riskAcknowledged = true }, c);
                await SubmitJob("/api/launch", new { id = CurrentBuild() }, c);
            }, true);
            launchState = Label(text["gameNotRunning"], 12, muted); launchState.VerticalAlignment = VerticalAlignment.Center; launchState.Margin = new Thickness(0, 0, 18, 0);
            launchButton.MinWidth = 160; launchButton.FontWeight = FontWeights.SemiBold; launchButton.Margin = new Thickness(0, 3, 0, 3);
            cancelButton = new Button { Content = text["cancel"], Padding = new Thickness(14, 9, 14, 9), Margin = new Thickness(0, 6, 10, 6) };
            cancelButton.Click += async (s, e) => await CancelOrRecover();
            var launchBar = new DockPanel(); DockPanel.SetDock(launchButton, Dock.Right); launchBar.Children.Add(launchButton);
            DockPanel.SetDock(cancelButton, Dock.Right); launchBar.Children.Add(cancelButton); launchBar.Children.Add(launchState); footer.Children.Add(launchBar);
            operationText = Label(preview ? text["preview"] : text["ready"], 12, muted);
            progress = new ProgressBar { Minimum = 0, Maximum = 1, Height = 4, Foreground = teal, Background = card, Margin = new Thickness(0, 8, 0, 5) };
            footer.Children.Add(progress); footer.Children.Add(operationText);
            recoveryShortcut = new Button { Content = text["processRecoveryTitle"], Tag = "processRecoveryOpen", HorizontalAlignment = HorizontalAlignment.Left };
            recoveryShortcut.Click += (s, e) => OpenProcessRecovery(); StyleFooter(recoveryShortcut); actions.Add(recoveryShortcut); footer.Children.Add(recoveryShortcut);
            details = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 65, MaxHeight = 110,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Background = card, Foreground = foreground, BorderThickness = new Thickness(0), Padding = new Thickness(10) };
            feedbackPanel = new Expander { Header = text["details"], Content = details, Foreground = muted, Visibility = Visibility.Collapsed };
            footer.Children.Add(feedbackPanel);
            var web = Action("web", async c => { await EnsureConnected(c); Open(bridge.Address); operationText.Text = text["webOpened"]; });
            web.Content = text["developerTools"]; StyleFooter(web);
            var footnotes = Wrap(GuideLink("guide", "guide.html", true), GuideLink("fullControls", "guide.html#controls", true),
                PageLink("troubleshooting", true), web, FooterLink("support", "https://ko-fi.com/chronohax"));
            footnotes.Margin = new Thickness(0, 5, 0, 0); footer.Children.Add(footnotes); DockPanel.SetDock(footer, Dock.Bottom); outer.Children.Add(footer);
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
                uninstallPanel.IsExpanded = uninstallOpen;
                details.Text = savedDetails ?? ""; operationText.Text = savedFeedback ?? text["ready"];
                feedbackPanel.IsExpanded = feedbackOpen;
            }
            Content = outer; rendering = false; ShowStatus();
            if (rebuilding)
                Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
                { (pageHost.Content as ScrollViewer)?.ScrollToVerticalOffset(scrollOffset); languages.Focus(); }));
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
        }
        ScrollViewer Page(UIElement body)
        { return new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Content = body }; }
        bool WantsInstall()
        { return store.Selected == null || SelectedRelease().id != store.Selected.release.id; }
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
            // Propose an update without installing it or changing the old helper.
            // Retain an already deliberate package selection and require consent.
            if (store.Selected != null && !WantsInstall())
            {
                Func<Release, DateTimeOffset> date = r => {
                    DateTimeOffset parsed;
                    return DateTimeOffset.TryParse(r.published ?? r.created, out parsed) ? parsed : DateTimeOffset.MinValue;
                };
                var newer = releases.Items.Cast<Release>().Where(r => r.id != store.Selected.release.id && date(r) > date(store.Selected.release))
                    .OrderByDescending(date).FirstOrDefault();
                if (newer != null) releases.SelectedItem = newer;
            }
            advancedPanel.IsExpanded = true;
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
            connectionPanel.Visibility = Visibility.Collapsed; panel.Children.Add(connectionPanel);
            gamePath = Label(text["noGame"], 12, muted); gamePath.TextWrapping = TextWrapping.NoWrap; gamePath.TextTrimming = TextTrimming.CharacterEllipsis;
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
            gameHint = Label(text[preview ? "locateHint" : "findingGame"], 11, muted);
            panel.Children.Add(Step("01", text["locate"], browse, gamePath, gameHint, gameLocationOptions));
            runtime = Label(text["runtimeHint"], 12, muted);
            runtimeCheck = Action("check", async c => { await EnsureConnected(c); await SubmitJob("/api/check", new { id = CurrentBuild() }, c); });
            runtimeHint = Label(text["runtimeInstallHint"], 11, muted);
            var runtimeStep = Step("03", text["runtime"], null, runtime,
                Wrap(Action("useHeadset", async c => await Runtime("headset", c)), Action("useSimulator", async c => await Runtime("simulator", c))), runtimeHint);
            releases = Choice(); foreach (var release in catalog.releases) releases.Items.Add(release);
            if (store.Selected != null && !releases.Items.Cast<Release>().Any(r => r.id == store.Selected.release.id)) releases.Items.Add(store.Selected.release);
            releases.SelectedItem = store.Selected == null ? releases.Items.Cast<Release>().FirstOrDefault() : releases.Items.Cast<Release>().First(r => r.id == store.Selected.release.id);
            catalogState = Label(text[catalogKey], 12, muted);
            packageState = Label(text["none"], 12, muted);
            gameVersion = Label(text["targetGame"] + " " + SelectedRelease().gameVersion, 13);
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
            })));
            updates.Children.Add(Label(text["selectedVersionAction"], 11, muted));
            advancedPanel = new Expander { Header = text["advanced"], Foreground = muted, Content = updates, Margin = new Thickness(0, 4, 0, 0) };
            risk = new CheckBox { Content = Label(text["riskAccept"], 12), Foreground = foreground, Margin = new Thickness(0, 3, 0, 0) };
            risk.Checked += (s, e) => ShowStatus(); risk.Unchecked += (s, e) => ShowStatus();
            riskNotice = Label(text["risk"], 12, Color("#ECCB93"));
            var consent = new StackPanel(); consent.Children.Add(riskNotice); consent.Children.Add(versionConsent); consent.Children.Add(risk);
            var notice = new Border { Child = consent, Padding = new Thickness(10, 4, 10, 5), CornerRadius = new CornerRadius(7), Background = Color("#272921"), Margin = new Thickness(0, 3, 0, 2) };
            launcherVersion = Label("", 12, muted);
            launcherUpdateButton = Action("updateLauncher", async c => await UpdateLauncher(c));
            packageUpdateState = Label("", 12, teal);
            packageUpdateButton = Action("selectLatestPackage", c => {
                var newer = NewerPackage();
                if (newer != null) { releases.SelectedItem = newer; advancedPanel.IsExpanded = true; operationText.Text = text["installNext"]; }
                return Task.CompletedTask;
            });
            panel.Children.Add(Step("02", text["package"], null, packageState, launcherVersion,
                Wrap(launcherUpdateButton), packageUpdateState, Wrap(packageUpdateButton), gameVersion, notice, advancedPanel));
            panel.Children.Add(runtimeStep);
            return panel;
        }
        UIElement Recording()
        {
            source = Choice(text["auto"], text["steamvr"], text["simulator"]);
            source.SelectionChanged += (s, e) => { if (!rendering) ShowStatus(); };
            fps = Choice("30", "45", "60"); size = Choice("720", "1024", "1280"); size.SelectedIndex = 1; purpose = Choice(text["videoOnly"], text["playtest"]);
            var panel = new StackPanel(); panel.Children.Add(Label(text["recordInfo"], 14, muted));
            recordingState = Label("", 12, teal); panel.Children.Add(recordingState);
            recordingOptions = new Expander { Header = text["recordOptions"], Foreground = muted, Margin = new Thickness(0, 12, 0, 0), Content = new StackPanel() };
            ((StackPanel)recordingOptions.Content).Children.Add(CaptureFields("source", source, "purpose", purpose));
            ((StackPanel)recordingOptions.Content).Children.Add(CaptureFields("fps", fps, "eyeSize", size));
            panel.Children.Add(Card(text["record"], Label(text["recordDefaults"], 12, muted),
                Wrap(Action("recordStart", async c =>
                {
                    await EnsureConnected(c);
                    await SubmitJob("/api/record-start", new { videoOnly = purpose.SelectedIndex == 0, fps = int.Parse((string)fps.SelectedItem), eyeWidth = int.Parse((string)size.SelectedItem), source = new[] { "auto", "steamvr", "simulator" }[source.SelectedIndex] }, c);
                }, true), Action("recordStop", async c => { await EnsureConnected(c); await bridge.Post("/api/record-stop", new { }, c); }),
                Action("openRecordings", async c => { await EnsureConnected(c); await bridge.Post("/api/open", new { folder = "recordings" }, c); operationText.Text = text["folderOpened"]; })), recordingOptions));
            panel.Children.Add(Label(text["recordPrivacy"], 12, muted)); return panel;
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
            var keys = Label(input, 12, teal); keys.FontWeight = FontWeights.SemiBold; row.Children.Add(keys);
            var description = Label(text[key], 12, muted); Grid.SetColumn(description, 1); row.Children.Add(description); return row;
        }
        UIElement Help()
        {
            var panel = new StackPanel();
            panel.Children.Add(PageLink("backToSetup", false));
            panel.Children.Add(Label(text["troubleshooting"], 22));
            panel.Children.Add(Label(text["troubleshootingIntro"], 12, muted));
            panel.Children.Add(ControllerCheck());
            panel.Children.Add(Wrap(runtimeCheck, Action("copyDiagnostics", async c => await CopyDiagnostics(c)), GuideLink("report", "feedback.html")));
            panel.Children.Add(ProcessRecovery());
            installed = Choice(); foreach (var item in store.State.installed) installed.Items.Add(item);
            installed.SelectedItem = store.Selected;
            installed.SelectionChanged += (s, e) => ShowStatus();
            var recovery = new StackPanel { Margin = new Thickness(8) };
            recovery.Children.Add(Card(text["installed"], installed, Label(text["recoveryInfo"], 13, muted),
                Wrap(Action("useVersion", async c =>
                {
                    var item = installed.SelectedItem as Installed; if (item == null) throw new InvalidOperationException(text["chooseVersion"]);
                    await Task.Run(() => PackageStore.Verify(store.Folder(item), item.release, c), c);
                    await PreparePackageChange(c); store.Select(item); PopulateInstalled(); await Connect(c);
                }), Action("repair", async c =>
                {
                    var item = installed.SelectedItem as Installed;
                    if (item == null) throw new InvalidOperationException(text["chooseVersion"]);
                    await InstallRelease(item.release, c);
                }),
                Action("rollback", async c =>
                {
                    var prior = store.State.installed.FirstOrDefault(x => x.folder == store.State.previous);
                    if (prior == null) throw new InvalidOperationException(text["noPrevious"]);
                    await Task.Run(() => PackageStore.Verify(store.Folder(prior), prior.release, c), c);
                    await PreparePackageChange(c); store.Select(prior); PopulateInstalled(); await Connect(c);
                })),
                Wrap(Action("restore", async c =>
                {
                    if (!Confirm("restoreConfirm")) return;
                    await EnsureConnected(c); await SubmitJob("/api/restore", new { }, c);
                }), Action("reset", async c =>
                {
                    if (!Confirm("resetConfirm")) return;
                    await EnsureConnected(c); await SubmitJob("/api/apply", new { id = CurrentBuild(), reset = true }, c);
                }), Action("remove", async c => await Remove(c)))));
            recovery.Children.Add(Wrap(Action("scanGame", async c => await DiscoverGame(c)), Action("openLogs", c =>
            {
                if (!Directory.Exists(bridge.Data)) throw new DirectoryNotFoundException(text["logsMissing"]);
                Open(bridge.Data); operationText.Text = text["folderOpened"]; return Task.CompletedTask;
            }), Action("portable", async c => await OpenPortable(c))));
            recoveryPanel = new Expander { Header = text["recovery"], Foreground = foreground, Content = recovery, Margin = new Thickness(0, 0, 0, 14) };
            panel.Children.Add(recoveryPanel);
            uninstallResultText = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
                MinHeight = 50, MaxHeight = 200, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = card, Foreground = foreground, BorderThickness = new Thickness(0), Padding = new Thickness(8), Text = uninstallDetails,
                Visibility = String.IsNullOrWhiteSpace(uninstallDetails) ? Visibility.Collapsed : Visibility.Visible };
            uninstallPanel = new Expander { Header = text["prepareUninstall"], Foreground = foreground, Margin = new Thickness(0, 0, 0, 14),
                Content = Card(text["prepareUninstall"], Label(text["uninstallIntro"], 12, muted),
                    Wrap(ControllerButton("prepareUninstall", async () => {
                        await Run(PrepareUninstall); if (uninstallNeedsRecovery) OpenProcessRecovery();
                    })), uninstallResultText) };
            panel.Children.Add(uninstallPanel);
            panel.Children.Add(Label(text["privacy"], 12, muted)); return panel;
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
            processRecoveryState = Label("", 12, teal); processRecoveryRows = new StackPanel();
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
            controllerState = Label("", 13, teal);
            controllerProgress = new ProgressBar { Height = 3, IsIndeterminate = true, Foreground = teal, Background = card,
                Margin = new Thickness(0, 5, 0, 6), Visibility = Visibility.Collapsed };
            controllerDetails = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
                MinHeight = 90, MaxHeight = 210, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = background, Foreground = foreground, BorderThickness = new Thickness(0), Padding = new Thickness(10), FontSize = 12 };
            var guidance = new Expander { Header = text["controllerGuidance"], Foreground = foreground, Margin = new Thickness(0, 8, 0, 0),
                Content = new StackPanel() };
            var steps = (StackPanel)guidance.Content;
            steps.Children.Add(Label(text["controllerOnlyTitle"], 13, teal)); steps.Children.Add(Label(text["controllerOnlyHelp"], 12, muted));
            steps.Children.Add(Label(text["controllerTreadmillTitle"], 13, teal)); steps.Children.Add(Label(text["controllerTreadmillHelp"], 12, muted));
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
        string RedactDiagnostics(string report)
        {
            // Apply this to the complete report, including native exceptions and
            // helper logs. A helper's own redaction is not a clipboard boundary.
            report = Regex.Replace(report ?? "", @"(?im)(\b(?:user(?:name)?|computer(?:name)?|password|passwd|secret|token|api[ _-]?key|authorization|serial(?:[ _-]?number)?|device[ _-]?(?:id|instance))\b[""']?\s*[:=]\s*)[^\r\n]*", "$1[redacted]");
            report = Regex.Replace(report, @"(?i)(?:file:/+|[a-z]:[\\/]|\\\\|%(?:USERPROFILE|LOCALAPPDATA|APPDATA)%[\\/])[^\r\n""'<>|]*", "[path]");
            report = Regex.Replace(report, @"(?i)\b(?:HID|USB|BTHENUM|PCI)[\\#][^\s""'<>]+", "[device]");
            foreach (var value in new[] { Environment.UserName, Environment.MachineName })
                if (!String.IsNullOrWhiteSpace(value))
                    report = Regex.Replace(report, @"(?<![\w])" + Regex.Escape(value) + @"(?![\w])", "[private]", RegexOptions.IgnoreCase);
            return report;
        }
        async Task CopyDiagnostics(CancellationToken c)
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
            c.ThrowIfCancellationRequested(); report = RedactDiagnostics(report);
            if (diagnosticsCopyOverride != null) diagnosticsCopyOverride(report); else Clipboard.SetText(report);
            operationText.Text = text[backendAvailable ? "diagnosticsCopied" : "diagnosticsLocalOnly"];
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
        bool Confirm(string key)
        {
            bool accepted = confirmationOverride != null ? confirmationOverride(key) : MessageBox.Show(this, text[key], text["title"], MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
            if (!accepted) operationText.Text = text["notChanged"];
            return accepted;
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
                operationText.Text = text[catalogKey]; advancedPanel.IsExpanded = true;
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
        async Task UpdateLauncher(CancellationToken c)
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
                ShowStatus(); return;
            }
            if (!Confirm("launcherRestartConfirm")) return;
            // ApplyUpdatesAndRestart exits immediately. Persist settings first;
            // the service rechecks helper/native activity after the save.
            await launcherUpdates.RestartAsync(bridge.RequireLauncherUpdateIdle, store.Save, c);
        }
        void ShowLauncherUpdate()
        {
            string state = launcherUpdates.ReadyToRestart ? "launcherReady" : launcherUpdates.AvailableVersion != null ? "launcherAvailable" :
                !launcherUpdates.Installed ? "launcherPortable" : launcherUpdates.CheckFailed ? "launcherUpdateOffline" :
                launcherUpdates.CheckedOnce ? "launcherCurrent" : "launcherUpdateHint";
            launcherVersion.Text = String.Format(text["launcherVersion"], launcherUpdates.CurrentVersion) + " · " +
                String.Format(text[state], launcherUpdates.AvailableVersion);
            launcherUpdateButton.Content = text[launcherUpdates.ReadyToRestart ? "restartLauncher" : "updateLauncher"];
            launcherUpdateButton.Visibility = launcherUpdates.AvailableVersion == null ? Visibility.Collapsed : Visibility.Visible;
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
        Task Run(Func<CancellationToken, Task> action) { return RunAction(action, true); }
        async Task RunAction(Func<CancellationToken, Task> action, bool refreshAfter)
        {
            if (preview || operation != null) return;
            lastJobSnapshot = JobSnapshot(status);
            preserveActionFeedback = false; submittedJob = false;
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
            var newerPackage = NewerPackage();
            packageUpdateState.Text = newerPackage == null ? "" : String.Format(text["packageUpdateAvailable"], newerPackage.DisplayLabel);
            packageUpdateState.Visibility = newerPackage == null ? Visibility.Collapsed : Visibility.Visible;
            packageUpdateButton.Visibility = newerPackage == null || WantsInstall() ? Visibility.Collapsed : Visibility.Visible;
            var game = Json.Child(status, "game"); string launcher = Json.Text(game, "launcher");
            bool pendingChoice = !String.IsNullOrWhiteSpace(store.State.pendingLauncherPath);
            gamePath.Text = pendingChoice ? store.State.pendingLauncherPath : Json.Flag(game, "saved") && Json.Text(game, "mode") == "manual" ? text["manualStart"] : String.IsNullOrEmpty(launcher) ? (discovery == null ? store.State.launcherPath : discovery.Path) : launcher;
            if (String.IsNullOrEmpty(gamePath.Text)) gamePath.Text = text["noGame"];
            gamePath.ToolTip = gamePath.Text;
            string hint = discovery == null ? (preview ? "locateHint" : "findingGame") : discovery.Message;
            if (SteamChoiceNeedsPackage()) hint = "gameSteamUpdateRequired";
            if (!pendingChoice && Json.Flag(game, "saved")) hint = Json.Text(game, "mode") == "manual" ? "gameManualSaved" :
                String.IsNullOrEmpty(Json.Text(game, "problem")) ? Json.Text(game, "mode") == "steam" ? "gameSteamSaved" : "gameSavedFound" : "gameSavedMissing";
            gameHint.Text = text[hint];
            gameHint.ToolTip = discovery != null && discovery.Candidates.Count > 1 ? String.Join(Environment.NewLine, discovery.Candidates) : gameHint.Text;
            runtime.Text = store.Selected != null && !connectionReady ? text["connectionRequired"] : LauncherPresentation.RuntimeSummary(bridge.Address != null, status, k => text[k]);
            runtimeCheck.IsEnabled = operation == null && store.Selected != null;
            var job = Json.Child(status, "job"); var launch = Json.Child(status, "launch");
            bool workerRunning = LauncherBridge.LaunchWorkerRunning(status), running = Json.Flag(job, "running") || workerRunning;
            bool workerStalled = workerRunning && (Json.Flag(launch, "stalled") || Json.Flag(launch, "activityUnknown"));
            // A connection loss clears status for display but cannot prove that its
            // previously observed startup worker stopped. Resolve only from fresh status.
            if (connectionReady && status.ContainsKey("job")) unresolvedStartup = workerRunning || (Json.Flag(job, "running") && Json.Text(job, "kind") == "launch");
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
            launchState.Text = gameRunning || running || workerFailed || workerCancelled ? LauncherBridge.LaunchSummary(status, k => text[k]) :
                text[wantsInstall ? "installNext" : SteamChoiceNeedsPackage() ? "gameSteamUpdateRequired" : !connectionReady ? "connectionRequired" : injectorRunning ? "runtimeCloseFirst" : runtimeBlock ?? (risk.IsChecked != true ? "riskNext" : Json.Text(game, "mode") == "steam" ? "launchNextSteam" : "launchNext")];
            feedbackPanel.Visibility = String.IsNullOrWhiteSpace(details.Text) ? Visibility.Collapsed : Visibility.Visible;
            var capture = Json.Child(status, "recording");
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
                    button.BorderBrush = current ? teal : Brushes.Transparent;
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
                        case "selectLatestPackage": reason = running ? "busy" : NewerPackage() == null ? "noPackageUpdate" : null; break;
                        case "updateLauncher": reason = launcherUpdates.Busy ? "busy" : launcherUpdates.AvailableVersion == null ? "launcherUpdateHint" :
                            launcherUpdates.ReadyToRestart && (running || recording || gameRunning || injectorRunning) ? "launcherRestartIdle" : null; break;
                        case "launch": reason = running ? "busy" : gameRunning ? "alreadyRunning" : injectorRunning ? "runtimeCloseFirst" : wantsInstall ? (conflict ? "connectionRequired" : compatible.IsChecked != true ? "compatRequired" : null) : SteamChoiceNeedsPackage() ? "gameSteamUpdateRequired" : !connectionReady ? "connectionRequired" : runtimeBlock ?? (risk.IsChecked != true ? "riskAccept" : null); break;
                        case "recordStart": reason = !hasPackage ? "chooseVersion" : running || recording ? "busy" : !gameRunning ? "recordNeedsGame" : !canRecord ? "recordUnavailable" : null; break;
                        case "recordStop": reason = !recording ? "noRecording" : null; break;
                        case "repair": case "useVersion": case "remove": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : installed.SelectedItem == null ? "chooseVersion" : null; break;
                        case "rollback": reason = running ? "busy" : gameRunning || injectorRunning ? "runtimeCloseFirst" : store.State.previous == store.State.selected || !store.State.installed.Any(i => i.folder == store.State.previous) ? "noPrevious" : null; break;
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
                    if (conflict && new[] { "install", "repair", "useVersion", "rollback" }.Contains(key)) reason = "connectionRequired";
                }
                button.IsEnabled = reason == null;
                button.ToolTip = reason == null ? null : text[reason];
                if (key == "openExisting" || key == "switchLauncher") button.Visibility = conflict ? Visibility.Visible : Visibility.Collapsed;
            }
            UpdateControllerCheck();
            UpdateProcessRecovery();
            foreach (var choice in new[] { releases, installed, source, fps, size, purpose, gameLocations }) choice.IsEnabled = operation == null && !running;
            bool active = operation != null || running;
            recoveryShortcut.Visibility = unresolvedStartup || (hasPackage && !connectionReady) || workerFailed ? Visibility.Visible : Visibility.Collapsed;
            bool canCancel = operation != null || (bridge.Address != null && LauncherBridge.CanCancelLaunch(status));
            cancelButton.Content = text[operation == null && canCancel ? "stopWaiting" : "cancel"];
            cancelButton.IsEnabled = canCancel; cancelButton.Visibility = canCancel ? Visibility.Visible : Visibility.Collapsed;
            progress.Visibility = active && !(operation == null && workerStalled) ? Visibility.Visible : Visibility.Collapsed;
            if (operation == null)
            {
                progress.IsIndeterminate = running && !workerStalled;
                string snapshot = JobSnapshot(status);
                if (snapshot != lastJobSnapshot)
                {
                    // Consume each update once, including updates hidden by newer local feedback.
                    lastJobSnapshot = snapshot;
                    if (!preserveActionFeedback)
                    {
                        if (running)
                        {
                            operationText.Text = workerRunning && !String.IsNullOrWhiteSpace(Json.Text(launch, "message")) ? Json.Text(launch, "message") : Json.Text(job, "message");
                            if (workerRunning) { details.Text = StartupDetails(launch); feedbackPanel.Visibility = Visibility.Visible; if (workerStalled) feedbackPanel.IsExpanded = true; }
                        }
                        else if (workerFailed || workerCancelled)
                        {
                            string message = Json.Text(launch, "message");
                            operationText.Text = text[workerFailed ? "launchFailed" : "launchCancelled"] + (String.IsNullOrWhiteSpace(message) ? "" : " " + message);
                            details.Text = operationText.Text;
                            feedbackPanel.Visibility = Visibility.Visible; feedbackPanel.IsExpanded = workerFailed;
                        }
                        else if (Json.Flag(job, "error") || !String.IsNullOrEmpty(Json.Text(job, "code")))
                        {
                            string kind = Json.Text(job, "kind");
                            string output = Json.Text(job, "output").Trim();
                            string summary = output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).LastOrDefault() ?? "";
                            operationText.Text = text[Json.Flag(job, "error") ? "failed" : kind == "check" ? "checkComplete" : kind == "runtime" ? "runtimeComplete" : "actionComplete"] +
                                (String.IsNullOrEmpty(summary) ? "" : " " + (summary.Length > 180 ? summary.Substring(0, 177) + "…" : summary));
                            details.Text = Json.Text(job, "output");
                            feedbackPanel.Visibility = String.IsNullOrWhiteSpace(details.Text) ? Visibility.Collapsed : Visibility.Visible;
                            feedbackPanel.IsExpanded = Json.Flag(job, "error") || kind == "check";
                        }
                    }
                }
            }
        }
        public void SavePreview(string path)
        {
            var visual = (FrameworkElement)Content;
            visual.Measure(new Size(880, 790)); visual.Arrange(new Rect(0, 0, 880, 790)); visual.UpdateLayout();
            var image = new RenderTargetBitmap(880, 790, 96, 96, PixelFormats.Pbgra32); image.Render(visual);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            using (var stream = File.Create(path)) encoder.Save(stream);
        }
    }
}
