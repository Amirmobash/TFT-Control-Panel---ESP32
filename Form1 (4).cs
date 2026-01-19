using System.Drawing.Imaging;
using System.Windows.Forms.DataVisualization.Charting;
using System.Threading;
using System.Text.Json;
using System.Drawing.Printing;
using System.Text.RegularExpressions;
using static System.Runtime.InteropServices.JavaScript.JSType;

namespace Workshop
{
    public partial class Form1 : Form
    {
        // ---- Dynamic Items (12 items instead of fixed 4) ----
        private const int ItemCount = 12;
        private const int ShiftCount = 5;

        private sealed class ItemRow
        {
            public int Index { get; }
            public Panel Panel { get; }
            public TextBox NameBox { get; }
            public Label[] ShiftLabels { get; }

            public ItemRow(int index, Panel panel, TextBox nameBox, Label[] shiftLabels)
            {
                Index = index;
                Panel = panel;
                NameBox = nameBox;
                ShiftLabels = shiftLabels;
            }

            public int GetTotal()
            {
                int sum = 0;
                foreach (var lbl in ShiftLabels)
                {
                    if (int.TryParse(lbl.Text, out var v)) sum += v;
                }
                return sum;
            }

            public void ResetAllToZero()
            {
                for (int i = 0; i < ShiftLabels.Length; i++)
                    ShiftLabels[i].Text = "0";
            }
        }

        private FlowLayoutPanel? itemsHost;
        private readonly List<ItemRow> itemRows = new();

        // Results area (4 numbers) shown in one row at bottom-right
        private Panel? resultsHost;
        private FlowLayoutPanel? resultsRow;
        private const int ResultsHostHeight = 90;

        private System.Windows.Forms.Timer timer1;
        List<Chart> charts = new List<Chart>();
        string CoreFolder = null;
        bool havebox = false;
        double shiftStart1, shiftEnd1;
        double shiftStart2, shiftEnd2;
        double shiftStart3, shiftEnd3;
        double shiftStart4, shiftEnd4;
        int LiveScreenGap = 30000;
        // old fixed 4-items naming (blue/yellow/green/red) is still used by legacy controls,
        // but dynamic items use itemRows.
        string[] colors = { "blue", "yellow", "green", "red" };

        public Form1()
        {
            InitializeComponent();
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            if (keyData == Keys.Enter)
            {
                //  Enter i am amir emrooz 11092025
                return true;
            }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        public bool isNightShift()
        {
            return nightShift.Checked;
        }

        private void UpdateTotal()
        {
            totalNumber.Text = (chart5.Series[0].Points[0].YValues[0] + chart5.Series[0].Points[1].YValues[0] + chart5.Series[0].Points[2].YValues[0] + chart5.Series[0].Points[3].YValues[0] + chart5.Series[0].Points[4].YValues[0]) + "st";
            UpdateSTH();
        }
        //send data
        public void LiveScreenshot(object sender, EventArgs e)
        {
            // pooshe LiveScreenshot
            string liveScreenshotFolderPath = Path.Combine(CoreFolder, "LiveScreenshot");

            // pooshe hast ya na
            if (!Directory.Exists(liveScreenshotFolderPath))
            {
                // make
                Directory.CreateDirectory(liveScreenshotFolderPath);

                // hiden
                File.SetAttributes(liveScreenshotFolderPath, FileAttributes.Hidden);

            }
            try
            {
                this.WindowState = FormWindowState.Maximized;
                this.Invoke(new Action(() =>
                {
                    // Take a screenshot
                    using (Bitmap bitmap = new Bitmap(this.Width, this.Height))
                    {
                        this.DrawToBitmap(bitmap, new Rectangle(0, 0, this.Width, this.Height));
                        string fileName = Path.Combine(liveScreenshotFolderPath, $"{Environment.MachineName}.png");

                        // Save the file as PNG
                        bitmap.Save(fileName, ImageFormat.Png);
                    }
                }));
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error in Live_screenshot:\n{ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }

        }
        //send data
        public void LiveDataSender(object sender, EventArgs e)
        {
            string liveScreenshotFolderPath = Path.Combine(CoreFolder, "LiveData"); //  LiveScreenshot
            // pooshe
            if (!Directory.Exists(liveScreenshotFolderPath))
            {
                // make pooshe
                Directory.CreateDirectory(liveScreenshotFolderPath);
                // makhfi
                File.SetAttributes(liveScreenshotFolderPath, FileAttributes.Hidden);
            }

            string fileName = Path.Combine(liveScreenshotFolderPath, $"{Environment.MachineName}.json");
            var res = LiveDataManagerClass.SaveChartData(chart5, fileName, NameComboBox.Text, totalNumber.Text, totalBox.Text, totalHours.Text, STHLabel.Text);
            if (res != "true")
            {
                MessageBox.Show($"Error in Live_Daata\n{res}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            ///////////// dgv:
            string associatedName = NameComboBox.Text;
            string DGVDataFolderPath = Path.Combine(CoreFolder, "DGVData"); //  dgvData
            // pooshe
            if (!Directory.Exists(DGVDataFolderPath))
            {
                // make
                Directory.CreateDirectory(DGVDataFolderPath);
                // makhfi
                File.SetAttributes(DGVDataFolderPath, FileAttributes.Hidden);
            }
            string fullSavePath = Path.Combine(DGVDataFolderPath, associatedName + ".json");
            if (dataGridView1.RowCount > 0)
            {
                DGVDataManagerClass.SaveDataGridViewRows(dataGridView1, fullSavePath);
            }

        }
        private void UpdateBarColors()
        {
            UpdateTotal();
            foreach (Chart chart in charts)
            {
                foreach (var series in chart.Series)
                {
                    foreach (DataPoint point in series.Points)
                    {
                        if (point.YValues[0] >= 24) //mehr alls 24
                        {
                            point.Color = Color.Green; // gr
                        }
                        else
                        {
                            point.Color = Color.Red; // red
                        }
                    }
                }
                chart.Invalidate();
                chart.Refresh();
                chart.ResetAutoValues();
            }

        }

        // Hide all charts (nemoodar) from UI without breaking logic
        private void HideAllCharts()
        {
            try
            {
                // We keep charts for internal calculations / live-data JSON, but remove them from the screen.
                if (chart1 != null) { chart1.Visible = false; chart1.Enabled = false; }
                if (chart2 != null) { chart2.Visible = false; chart2.Enabled = false; }
                if (chart3 != null) { chart3.Visible = false; chart3.Enabled = false; }
                if (chart4 != null) { chart4.Visible = false; chart4.Enabled = false; }
                if (chart5 != null) { chart5.Visible = false; chart5.Enabled = false; }
            }
            catch
            {
                // ignore (designer changes, etc.)
            }
        }

        public bool ChooseFolder()
        {
            using (var fbd = new FolderBrowserDialog())
            {
                DialogResult result = fbd.ShowDialog();

                if (result == DialogResult.OK && !string.IsNullOrWhiteSpace(fbd.SelectedPath))
                {
                    CoreFolder = fbd.SelectedPath;
                    Properties.Settings.Default.SharedFolder = fbd.SelectedPath;
                    Properties.Settings.Default.Save(); // ذخیره دائمی
                    return true;
                }
                return false;
            }
        }

        public string GetOrAddCustomName(string _filePath)
        {
            string machineName = Environment.MachineName; // دریافت نام سیستم
            string customName = null;

            // خواندن تمام خطوط فایل
            var lines = File.ReadAllLines(_filePath);

            // جستجوی نام سیستم
            foreach (var line in lines)
            {
                if (line.StartsWith($"{machineName} -->"))
                {
                    // استخراج custom name از متن
                    customName = line.Split(new[] { "-->" }, StringSplitOptions.None)[1].Trim();
                    break;
                }
            }

            // اگر custom name پیدا نشد، مقدار پیش‌فرض را اضافه کنید
            if (string.IsNullOrEmpty(customName))
            {
                customName = machineName; // مقدار پیش‌فرض
                File.AppendAllText(_filePath, $"{machineName} --> {customName}{Environment.NewLine}");
            }

            return customName;
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            shiftStart1 = 5.5; shiftEnd1 = 7.5; // ساعت‌های شیفت روز
            shiftStart2 = 7.5; shiftEnd2 = 9.5;
            shiftStart3 = 9.5; shiftEnd3 = 11.5;
            shiftStart4 = 11.5; shiftEnd4 = 13.5;
            var tt = Environment.MachineName;
            if (string.IsNullOrEmpty(Properties.Settings.Default.SharedFolder))
            {
                ChooseFolder();
            }
            else
            {
                CoreFolder = Properties.Settings.Default.SharedFolder;
            }

            // NEW: make window bigger to eliminate scrolling
            try
            {
                this.FormBorderStyle = FormBorderStyle.Sizable;
                this.MaximizeBox = true;
                this.WindowState = FormWindowState.Maximized;
            }
            catch { }

            // NEW: show 4 results in one row at bottom-right and fit the 12-item list above it
            SetupResultsRow();
            SetupDynamicItemsHost();

            // Keep layouts updated on resize
            this.Resize += (_, __) =>
            {
                LayoutResultsHost();
                ResizeItemsHostToAvailableSpace();
            };
            this.BeginInvoke(new Action(() =>
            {
                LayoutResultsHost();
                ResizeItemsHostToAvailableSpace();
            }));


            if (CoreFolder != null)
            {
                try
                {//baraye item names(4 item)
                    var fileContent = File.ReadAllText(CoreFolder + "\\items.txt");
                    string[] parts = fileContent.Split(',');
                    // NEW: support 12 items
                    SetupDynamicItemsHost();
                    for (int i = 0; i < itemRows.Count && i < parts.Length; i++)
                    {
                        itemRows[i].NameBox.Text = parts[i].Trim();
                    }

                    // legacy 4 item textboxes (keep for backward compatibility)
                    if (parts.Length >= 4)
                    {
                        itemtextbox1.Text = parts[0];
                        itemtextbox2.Text = parts[1];
                        itemtextbox3.Text = parts[2];
                        itemtextbox4.Text = parts[3];
                    }
                }
                catch (Exception)
                {
                    MessageBox.Show("Error reading Items Name", "Warning", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                try
                {//baraye user names(name kargar ha)
                    // lesen ,  
                    string[] usernames = File.ReadAllText(CoreFolder + "\\usernames.txt").Split(',');
                    NameComboBox.Items.Clear();
                    // ا comboBox1  
                    NameComboBox.Items.AddRange(usernames);
                }
                catch (Exception)
                {
                    MessageBox.Show("Error reading User Names", "Warning", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                try
                {//baraye pc names
                    var pcName = GetOrAddCustomName(CoreFolder + "\\pcnames.txt");
                    pcNameLable.Text = pcName;
                }
                catch (Exception)
                {
                    MessageBox.Show("Error reading PC Name", "Warning", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                try
                {
                    // lesen JSON
                    string jsonContent = File.ReadAllText(Path.Combine(CoreFolder, "config.json"));

                    // Deserialize  JSON 
                    var config = JsonSerializer.Deserialize<Config>(jsonContent);

                    //  LiveScreenGap
                    LiveScreenGap = config.LiveScreenGap;

                }
                catch { }
            }
            // legacy charts are not required for dynamic items. keep only total chart.
            charts.Clear();
            DateLabel.Text = DateTime.Now.ToString("yyyy/MM/dd");

            charts.Add(chart5);
            chart5.ChartAreas[0].AxisX.MajorGrid.Enabled = false;
            chart5.ChartAreas[0].AxisY.MajorGrid.Enabled = false;
            chart5.ChartAreas[0].AxisX.CustomLabels.Add(0.5, 1.5, "5:30-7:30");
            chart5.ChartAreas[0].AxisX.CustomLabels.Add(1.5, 2.5, "7:30-9:30");
            chart5.ChartAreas[0].AxisX.CustomLabels.Add(2.5, 3.5, "9:30-11:30");
            chart5.ChartAreas[0].AxisX.CustomLabels.Add(3.5, 4.5, "11:30-13:30");
            chart5.ChartAreas[0].AxisX.CustomLabels.Add(4.5, 5.5, "---");
            UpdateBarColors();
            
            HideAllCharts();
//System.Threading.Timer timer = new System.Threading.Timer(LiveScreenshot, null, 0, 2000); // (2 start)


            if (LiveScreenGap > 1000)
            {
                timer1 = new System.Windows.Forms.Timer();
                timer1.Tick += new EventHandler(LiveDataSender);
                timer1.Interval = LiveScreenGap; // in miliseconds
                timer1.Start();
            }



            //chart5.Series[0].Points[0].YValues[0] = 39;
            //chart5.Series[0].Points[1].YValues[0] = 84;
            //chart5.Series[0].Points[2].YValues[0] = 55;
            //chart5.Series[0].Points[3].YValues[0] = 47;

            //for (int i = 0; i < 24; i++)
            //{
            //    string gg = "";
            //    var sum = 0;
            //    for (int j = 0; j < 4; j++)
            //    {
            //        var adad = new Random().Next(0, 20).ToString();
            //        gg += adad;
            //        sum += int.Parse( adad);
            //        if (j !=3)
            //        {
            //            gg += " + ";
            //        }
            //    }
            //    int rowIndex = dataGridView1.Rows.Add(new Random().Next(10000000, 100000000).ToString());
            //    dataGridView1.Rows[rowIndex].Cells[1].Value = gg;
            //    dataGridView1.Rows[rowIndex].Cells[2].Value = sum;
            //    dataGridView1.Rows[rowIndex].Cells[3].Value = new Random().Next(101, 999).ToString();
            //    dataGridView1.Rows[rowIndex].Cells[5].Value = "21:51";
            //}



        }

        private void AddBoxButton_Click(object sender, EventArgs e)
        {
            var neme = NameComboBox.Text;
            var start = startTimeTextBox.Text;
            var end = endTimeTextBox.Text;
            if (string.IsNullOrEmpty(neme))
            {
                MessageBox.Show("Please Select Name First");
                return;
            }
            if (string.IsNullOrEmpty(start))
            {
                MessageBox.Show("Please Select Start Time First");
                return;
            }
            if (string.IsNullOrEmpty(end))
            {
                MessageBox.Show("Please Select End Time First");
                return;
            }
            BarcodeTextBox.Enabled = true;
            BarcodeTextBox.Focus();
            AddBoxButton.Enabled = false;
        }

        private void BarcodeTextBox_TextChanged(object sender, EventArgs e)
        {
            if (BarcodeTextBox.Text.Length < 8)
                return;
            havebox = true;
            AddBoxButton.Enabled = true;
            int rowIndex = dataGridView1.Rows.Add(BarcodeTextBox.Text);
            dataGridView1.Rows[rowIndex].Cells["createTime"].Value = DateTime.Now.ToString("HH:mm");
            BarcodeTextBox.Enabled = false;
            BarcodeTextBox.Clear();
            Updatedgv();
            Zero();
            totalBox.Text = dataGridView1.RowCount.ToString() + "AB";
        }

        public void Zero()
        {
            // NEW: reset dynamic items if available
            if (itemRows.Count > 0)
            {
                foreach (var row in itemRows)
                    row.ResetAllToZero();

                for (int i = 0; i < ShiftCount; i++)
                    chart5.Series[0].Points[i].YValues[0] = 0;
            }
            else
            {
                // legacy 4 items
                for (int i = 0; i < 5; i++)
                {
                    string[] colors = { "blue", "yellow", "green", "red" };
                    foreach (var color in colors)
                    {
                        Control textLabel = this.Controls.Find(color + (i + 1), true)[0];
                        textLabel.Text = "0";
                    }
                }
            }
            UpdateBarColors();
            Updatedgv();
        }

        public void Updatedgv()
        {
            try
            {
                int lastRowIndex = dataGridView1.Rows.Count - 1;

                // NEW: dynamic items
                if (itemRows.Count > 0)
                {
                    var totals = itemRows.Select(r => r.GetTotal()).ToArray();
                    var sum = totals.Sum();
                    dataGridView1.Rows[lastRowIndex].Cells[1].Value = string.Join(" + ", totals);
                    dataGridView1.Rows[lastRowIndex].Cells[2].Value = sum.ToString();
                    return;
                }

                // legacy 4 items
                var blue = Convert.ToInt32(blue1.Text) + Convert.ToInt32(blue2.Text) + Convert.ToInt32(blue3.Text) + Convert.ToInt32(blue4.Text) + Convert.ToInt32(blue5.Text);
                var yellow = Convert.ToInt32(yellow1.Text) + Convert.ToInt32(yellow2.Text) + Convert.ToInt32(yellow3.Text) + Convert.ToInt32(yellow4.Text) + Convert.ToInt32(yellow5.Text);
                var green = Convert.ToInt32(green1.Text) + Convert.ToInt32(green2.Text) + Convert.ToInt32(green3.Text) + Convert.ToInt32(green4.Text) + Convert.ToInt32(green5.Text);
                var red = Convert.ToInt32(red1.Text) + Convert.ToInt32(red2.Text) + Convert.ToInt32(red3.Text) + Convert.ToInt32(red4.Text) + Convert.ToInt32(red5.Text);
                dataGridView1.Rows[lastRowIndex].Cells[1].Value = $"{blue} + {yellow} + {green} + {red}";
                dataGridView1.Rows[lastRowIndex].Cells[2].Value = $"{blue + yellow + green + red}";
            }
            catch (Exception)
            {
            }
        }






        private void startTimeButtons(object sender, EventArgs e)
        {
            var buttonText = (sender as Button).Text;
            if (buttonText.Length < 3)
                startTimeTextBox.Text = $"{buttonText}:00";
            else
                startTimeTextBox.Text = buttonText;
        }

        private void endTimeButtons(object sender, EventArgs e)
        {
            var buttonText = (sender as Button).Text;
            if (buttonText.Length < 3)
                endTimeTextBox.Text = $"{buttonText}:00";
            else
                endTimeTextBox.Text = buttonText;
        }

        private void nightShift_CheckedChanged(object sender, EventArgs e)
        {
            if (isNightShift())
            {
                shiftStart1 = 13.5; shiftEnd1 = 15.5; // spat
                shiftStart2 = 15.5; shiftEnd2 = 17.5;
                shiftStart3 = 17.5; shiftEnd3 = 19.5;
                shiftStart4 = 19.5; shiftEnd4 = 21.5;
                chart5.ChartAreas[0].AxisX.CustomLabels[0].Text = "13:30-15:30";
                chart5.ChartAreas[0].AxisX.CustomLabels[1].Text = "15:30-17:30";
                chart5.ChartAreas[0].AxisX.CustomLabels[2].Text = "17:30-19:30";
                chart5.ChartAreas[0].AxisX.CustomLabels[3].Text = "19:30-21:30";
            }
            else
            {
                shiftStart1 = 5.5; shiftEnd1 = 7.5; // tag
                shiftStart2 = 7.5; shiftEnd2 = 9.5;
                shiftStart3 = 9.5; shiftEnd3 = 11.5;
                shiftStart4 = 11.5; shiftEnd4 = 13.5;
                chart5.ChartAreas[0].AxisX.CustomLabels[0].Text = "5:30-7:30";
                chart5.ChartAreas[0].AxisX.CustomLabels[1].Text = "7:30-9:30";
                chart5.ChartAreas[0].AxisX.CustomLabels[2].Text = "9:30-11:30";
                chart5.ChartAreas[0].AxisX.CustomLabels[3].Text = "11:30-13:30";
            }
        }

        public void takeScreenshot(bool ShowMessageBox = true, bool print = false, bool ShowSuccessfullResault = false)
        {
            try
            {
                DialogResult confirmResult;
                if (ShowMessageBox)
                {
                    // Ask for confirmation
                    confirmResult = MessageBox.Show(
                       "Are you sure you want to take a screenshot and save it?",
                       "Confirm Screenshot",
                       MessageBoxButtons.YesNo,
                       MessageBoxIcon.Question);
                }
                else
                {
                    confirmResult = DialogResult.Yes;
                }


                if (confirmResult == DialogResult.Yes)
                {
                    // Define the file path
                    string folderPath = CoreFolder + @"\Screenshots\" + Environment.MachineName;
                    if (!Directory.Exists(folderPath))
                    {
                        Directory.CreateDirectory(folderPath);
                    }

                    // Take a screenshot
                    using (Bitmap bitmap = new Bitmap(this.Width, this.Height))
                    {
                        this.DrawToBitmap(bitmap, new Rectangle(0, 0, this.Width, this.Height));
                        if (!print)
                        {//save img
                            string fileName = Path.Combine(folderPath, $"Screenshot_{DateTime.Now:yyyyMMdd_HHmmss}.png");
                            // Save the file as PNG
                            bitmap.Save(fileName, ImageFormat.Png);
                            // Show success message
                            if (ShowSuccessfullResault)
                                MessageBox.Show($"Screenshot saved successfully:\n{fileName}", "Success", MessageBoxButtons.OK, MessageBoxIcon.Information);
                        }
                        else if (print)
                        {//print instead of save
                         // sanad
                            PrintDocument printDoc = new PrintDocument();
                            printDoc.PrintPage += (s, ev) =>
                            {
                                ev.Graphics.DrawImage(bitmap, ev.MarginBounds);
                            };

                            //// printer
                            //PrintDialog printDialog = new PrintDialog();
                            //printDialog.Document = printDoc;

                            //if (printDialog.ShowDialog() == DialogResult.OK)
                            //{
                            try
                            {
                                printDoc.Print(); // send to printer
                            }
                            catch (Exception ex)
                            {
                                MessageBox.Show(" drucker fehlermeldung " + ex.Message);
                            }
                            //}
                        }

                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error while saving the screenshot:\n{ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        //send
        private void button16_Click(object sender, EventArgs e)
        {
            takeScreenshot(false);
        }
        //enter
        private void button32_Click(object sender, EventArgs e)
        {
            if (BarcodeTextBox.Text == "")
                return;
            havebox = true;
            AddBoxButton.Enabled = true;
            dataGridView1.Rows.Add(BarcodeTextBox.Text);
            BarcodeTextBox.Enabled = false;
            BarcodeTextBox.Clear();
            Updatedgv();
            Zero();
        }

        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            if (MessageBox.Show("Are you sure you want to close?", "Confirm exit", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.No)
                e.Cancel = true;
            else
            {//closing
                takeScreenshot(false);
                try
                {
                    var nowHour = DateTime.Now.ToString("HH-mm");
                    ReportClass report = new ReportClass
                    {
                        name = NameComboBox.Text,
                        date = DateLabel.Text,
                        shiftNiight = nightShift.Checked,
                        totalItems = Convert.ToInt32(new string(totalNumber.Text.Where(char.IsDigit).ToArray())),
                        totalBoxes = Convert.ToInt32(new string(totalBox.Text.Where(char.IsDigit).ToArray())),
                        totalHours = Regex.Replace(totalHours.Text, @"[^0-9:]", ""),
                        startTime = TimeSpan.Parse(startTimeTextBox.Text),
                        endTime = TimeSpan.Parse(endTimeTextBox.Text),
                        ReportTime = nowHour
                    };
                    // Report
                    string ReportsFolderPath = Path.Combine(CoreFolder, "Reports");

                    // pooshe
                    if (!Directory.Exists(ReportsFolderPath))
                    {
                        // make
                        Directory.CreateDirectory(ReportsFolderPath);
                        // makhfi
                        File.SetAttributes(ReportsFolderPath, FileAttributes.Hidden);
                    }
                    // zibaee(indent)
                    var options = new JsonSerializerOptions
                    {
                        WriteIndented = true
                    };

                    // JSON
                    string json = JsonSerializer.Serialize(report, options);

                    var todayDateFolder = Path.Combine(ReportsFolderPath, DateLabel.Text.Replace('/', '-').Replace('.', '-'));
                    if (!Directory.Exists(todayDateFolder))
                        Directory.CreateDirectory(todayDateFolder);
                    // save
                    var fileName = Path.Combine(todayDateFolder, Environment.MachineName + nowHour + ".json");
                    File.WriteAllText(fileName, json);
                }
                catch (Exception)
                {
                    MessageBox.Show("Error Creating Report!");
                }

            }
        }

        private void chart1_Click(object sender, EventArgs e)
        {

        }

        private void itemtextbox2_TextChanged(object sender, EventArgs e)
        {

        }

        private void red5_Click(object sender, EventArgs e)
        {

        }

        private void button17_Click(object sender, EventArgs e)
        {

        }

        private void blue3_Click(object sender, EventArgs e)
        {

        }

        private void label6_Click(object sender, EventArgs e)
        {

        }

        private void startTimeTextBox_TextChanged(object sender, EventArgs e)
        {
            var _2char = startTimeTextBox.Text.Substring(0, 2);
            var _1char = startTimeTextBox.Text.Substring(0, 1);
            try
            {
                if (int.Parse(_2char) >= 11)
                    nightShift.Checked = true;
                else
                    nightShift.Checked = false;
            }
            catch (Exception)
            {
                try
                {
                    if (int.Parse(_1char) >= 11)
                        nightShift.Checked = true;
                    else
                        nightShift.Checked = false;
                }
                catch (Exception) { }
            }
            calculateHours();
            UpdateSTH();
        }

        private void endTimeTextBox_TextChanged(object sender, EventArgs e)
        {
            calculateHours();
            UpdateSTH();
        }

        public void calculateHours()
        {
            if (endTimeTextBox.Text.Length >= 4 && startTimeTextBox.Text.Length >= 4)
            {
                try
                {
                    TimeSpan start = TimeSpan.Parse(startTimeTextBox.Text);
                    TimeSpan end = TimeSpan.Parse(endTimeTextBox.Text);
                    TimeSpan difference = end - start;
                    string workedTime = difference.Hours + (difference.Minutes != 0 ? ":" + difference.Minutes : "");
                    totalHours.Text = workedTime + " std";
                }
                catch (Exception)
                {
                }
            }
        }
        public void UpdateSTH()
        {
            if (endTimeTextBox.Text.Length < 4 && startTimeTextBox.Text.Length < 4)
                return;
            try
            {
                var st = (chart5.Series[0].Points[0].YValues[0] + chart5.Series[0].Points[1].YValues[0] + chart5.Series[0].Points[2].YValues[0] + chart5.Series[0].Points[3].YValues[0] + chart5.Series[0].Points[4].YValues[0]);
                TimeSpan start = TimeSpan.Parse(startTimeTextBox.Text);
                TimeSpan end = TimeSpan.Parse(endTimeTextBox.Text);
                TimeSpan difference = end - start;
                int sth = (int)(st / (difference.TotalMinutes / 60));
                STHLabel.Text = sth + " st/std";
            }
            catch (Exception)
            {
            }
        }

        private void PrintButton_Click(object sender, EventArgs e)
        {
            takeScreenshot(false, true);
        }

        private void PosButton_Click(object sender, EventArgs e)
        {
            if (dataGridView1.Rows.Count > 0)
            {
                // NewRow اگه AllowUserToAddRows 
                int lastRowIndex = dataGridView1.AllowUserToAddRows ? dataGridView1.Rows.Count - 2 : dataGridView1.Rows.Count - 1;

                // selool
                dataGridView1.Rows[lastRowIndex].Cells[3].Value = PosTextBox.Text;
            }
            PosTextBox.Clear();
        }

        public int whatShiftIsThisTime(string time)
        {
            switch (Convert.ToInt32(time.Split(':')[0]) + (Convert.ToInt32(time.Split(':')[1]) / 60.0))
            {
                case double hour when hour >= shiftStart1 && hour < shiftEnd1:
                    return 1;
                case double hour when hour >= shiftStart2 && hour < shiftEnd2:
                    return 2;
                case double hour when hour >= shiftStart3 && hour < shiftEnd3:
                    return 3;
                case double hour when hour >= shiftStart4 && hour < shiftEnd4:
                    return 4;
                default:
                    return 5;
            }
        }
        private void dataGridView1_CellContentClick(object sender, DataGridViewCellEventArgs e)//del
        {
            // barrasi
            if (dataGridView1.Columns[e.ColumnIndex] is DataGridViewButtonColumn && e.RowIndex >= 0)
            {
                if (MessageBox.Show($"Delete Box With Barcode{dataGridView1.Rows[e.RowIndex].Cells["Barcode"].Value}?", "Delete?", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes)
                {
                    string? boxSum = dataGridView1.Rows[e.RowIndex].Cells["res"].Value.ToString();
                    if (e.RowIndex == dataGridView1.Rows.Count - 1)//hazf
                    {
                        int rowsCount = dataGridView1.Rows.Count;
                        if (rowsCount > 1)//faal
                        {
                            string[] previousBoxItems = dataGridView1.Rows[rowsCount - 2].Cells["Sum"].Value.ToString().Split(" + ");
                            string? previousBoxTime = dataGridView1.Rows[rowsCount - 2].Cells["createTime"].Value.ToString();
                            string? previousBoxSum = dataGridView1.Rows[rowsCount - 2].Cells["res"].Value.ToString();
                            int number = whatShiftIsThisTime(previousBoxTime);
                            // NEW: dynamic items (12)
                            if (itemRows.Count > 0)
                            {
                                // clear UI (only labels, do NOT touch chart5 here)
                                foreach (var row in itemRows)
                                    row.ResetAllToZero();

                                for (int i = 0; i < itemRows.Count && i < previousBoxItems.Length; i++)
                                {
                                    // number is 1..5 => shift index is number-1
                                    itemRows[i].ShiftLabels[number - 1].Text = previousBoxItems[i];
                                }
                            }
                            else
                            {
                                // legacy 4 items
                                for (int i = 0; i < 4; i++)//farbe adad
                                {
                                    Label? targetLabel = this.Controls.Find(colors[i] + number, true).FirstOrDefault() as Label;

                                    if (targetLabel != null)
                                    {
                                        targetLabel.Text = previousBoxItems[i];
                                    }
                                    else
                                    {
                                        MessageBox.Show($"Error Fiinding {colors[i] + number}");
                                    }
                                }
                            }
                            chart5.Series[0].Points[number - 1].YValues[0] -= Convert.ToInt32(boxSum);
                            UpdateBarColors();
                        }
                        else//sefre
                        {
                            // if it was the only row, reset everything
                            Zero();
                            UpdateBarColors();
                        }
                    }
                    else//hazf
                    {
                        string? boxCreateTime = dataGridView1.Rows[e.RowIndex].Cells["createTime"].Value.ToString();
                        chart5.Series[0].Points[whatShiftIsThisTime(boxCreateTime) - 1].YValues[0] -= Convert.ToInt32(boxSum);
                        UpdateBarColors();
                    }
                    dataGridView1.Rows.RemoveAt(e.RowIndex);
                    totalBox.Text = dataGridView1.RowCount.ToString() + "AB";
                }

            }
        }

        // -------------------- Results layout (4 numbers at bottom in one row) --------------------
        private void SetupResultsRow()
        {
            if (resultsHost != null) return;

            // Create a bottom-right host panel (same width as items area)
            resultsHost = new Panel
            {
                Name = "resultsHost",
                Height = ResultsHostHeight,
                BackColor = this.BackColor,
                Anchor = AnchorStyles.Right | AnchorStyles.Bottom
            };

            resultsRow = new FlowLayoutPanel
            {
                Name = "resultsRow",
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.LeftToRight,
                WrapContents = false,
                AutoScroll = false,
                RightToLeft = RightToLeft.Yes, // align items to the right
                Padding = new Padding(10, 10, 10, 10)
            };

            // Move the existing labels into the row (keep their text updates working)
            try
            {
                totalNumber.AutoSize = true;
                totalBox.AutoSize = true;
                totalHours.AutoSize = true;
                STHLabel.AutoSize = true;

                totalNumber.Margin = new Padding(30, 0, 0, 0);
                totalBox.Margin = new Padding(30, 0, 0, 0);
                totalHours.Margin = new Padding(30, 0, 0, 0);
                STHLabel.Margin = new Padding(30, 0, 0, 0);

                totalNumber.TextAlign = ContentAlignment.MiddleCenter;
                totalBox.TextAlign = ContentAlignment.MiddleCenter;
                totalHours.TextAlign = ContentAlignment.MiddleCenter;
                STHLabel.TextAlign = ContentAlignment.MiddleCenter;

                resultsRow.Controls.Add(totalNumber);
                resultsRow.Controls.Add(totalBox);
                resultsRow.Controls.Add(totalHours);
                resultsRow.Controls.Add(STHLabel);
            }
            catch
            {
                // ignore if designer names changed
            }

            resultsHost.Controls.Add(resultsRow);
            this.Controls.Add(resultsHost);
            resultsHost.BringToFront();
            LayoutResultsHost();
        }

        private void LayoutResultsHost()
        {
            if (resultsHost == null) return;

            try
            {
                // keep it aligned with the right-side items area
                resultsHost.Width = panel1.Width;
                resultsHost.Left = panel1.Left;

                int bottomPadding = 8;
                resultsHost.Top = this.ClientSize.Height - resultsHost.Height - bottomPadding;

                // also ensure it is not hidden behind other controls
                resultsHost.BringToFront();
            }
            catch
            {
                // ignore
            }
        }

        // -------------------- Dynamic items helpers --------------------
        private void SetupDynamicItemsHost()
        {
            if (itemsHost != null) return; // already created

            // hide legacy 4 item panels (they are still in the Designer)
            try
            {
                panel1.Visible = false;
                panel2.Visible = false;
                panel3.Visible = false;
                panel4.Visible = false;
            }
            catch { }

            itemsHost = new FlowLayoutPanel
            {
                Name = "itemsHost",
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = false,
                Location = panel1.Location,
                // size will be recalculated in ResizeItemsHostToAvailableSpace()
                Size = new Size(panel1.Width, 400),
                Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right,
                Margin = new Padding(0),
                Padding = new Padding(0)
            };

            this.Controls.Add(itemsHost);
            itemsHost.BringToFront();
            // Keep results visible above the list
            if (resultsHost != null) resultsHost.BringToFront();
            ResizeItemsHostToAvailableSpace();

            itemRows.Clear();
            for (int i = 0; i < ItemCount; i++)
            {
                var row = CreateItemRow(i);
                itemRows.Add(row);
                itemsHost.Controls.Add(row.Panel);
            }
        }

        private void ResizeItemsHostToAvailableSpace()
        {
            if (itemsHost == null) return;

            try
            {
                // available space = from itemsHost top to the top of resultsHost (results area starts there)
                int top = itemsHost.Top;
                int bottomLimit = (resultsHost != null && resultsHost.Visible) ? (resultsHost.Top - 10) : (totalNumber.Top - 10);
                int h = bottomLimit - top;

                // safety
                if (h < 120) h = 120;

                itemsHost.Width = panel1.Width;
                itemsHost.Height = h;

                // Make sure ALL rows fit without any scroll: auto-calc row height based on available height.
                const int rowGap = 6;
                int rows = Math.Max(1, itemRows.Count);
                int availableForRows = itemsHost.ClientSize.Height - (rowGap * (rows - 1));
                int rowHeight = availableForRows / rows;
                if (rowHeight < 34) rowHeight = 34;

                foreach (var r in itemRows)
                {
                    r.Panel.Width = itemsHost.ClientSize.Width;
                    r.Panel.Height = rowHeight;

                    // Vertically center controls so they still look OK even when row height becomes smaller.
                    if (r.NameBox != null)
                    {
                        int nt = (rowHeight - r.NameBox.Height) / 2;
                        if (nt < 2) nt = 2;
                        r.NameBox.Top = nt;
                    }

                    foreach (Control c in r.Panel.Controls)
                    {
                        if (c is Label lbl)
                        {
                            int lt = (rowHeight - lbl.Height) / 2;
                            if (lt < 2) lt = 2;
                            lbl.Top = lt;
                        }
                        else if (c is Button b)
                        {
                            int bh = Math.Min(40, rowHeight - 10);
                            if (bh < 24) bh = 24;
                            b.Height = bh;
                            int bt = (rowHeight - b.Height) / 2;
                            if (bt < 2) bt = 2;
                            b.Top = bt;
                        }
                    }
                }
            }
            catch
            {
                // ignore
            }
        }

        private ItemRow CreateItemRow(int index)
        {
            // background colors (repeat)
            Color[] rowColors = new[]
            {
                Color.Orange, Color.MediumTurquoise, Color.Gold, Color.LightSkyBlue,
                Color.LightPink, Color.Khaki, Color.LightGreen, Color.Plum,
                Color.SandyBrown, Color.PaleTurquoise, Color.LightSalmon, Color.PaleGreen
            };

            var panel = new Panel
            {
                Width = panel1.Width,
                Height = panel1.Height,
                BackColor = rowColors[index % rowColors.Length],
                Margin = new Padding(0, 0, 0, 6)
            };

            var nameBox = new TextBox
            {
                Enabled = false,
                Font = itemtextbox1.Font,
                BackColor = panel.BackColor,
                Location = new Point(22, 18),
                Size = new Size(122, 29),
                Text = $"item {index + 1}"
            };
            panel.Controls.Add(nameBox);

            // shift labels (5)
            var labels = new Label[ShiftCount];
            int[] xs = new[] { 169, 210, 250, 290, 330 };
            for (int i = 0; i < ShiftCount; i++)
            {
                labels[i] = new Label
                {
                    AutoSize = true,
                    Font = blue1.Font,
                    Location = new Point(xs[i], 22),
                    Text = "0"
                };
                panel.Controls.Add(labels[i]);
            }

            // buttons: -1, +5, +1
            var btnM1 = MakeDeltaButton("-1", -1, new Point(382, 8), foreColor: Color.Red);
            var btnP5 = MakeDeltaButton("+5", +5, new Point(491, 8), foreColor: Color.Green);
            var btnP1 = MakeDeltaButton("+1", +1, new Point(597, 8), foreColor: Color.Green);

            // store item index in Tag
            btnM1.Tag = Tuple.Create(index, -1);
            btnP5.Tag = Tuple.Create(index, +5);
            btnP1.Tag = Tuple.Create(index, +1);

            btnM1.Click += ItemDeltaButton_Click;
            btnP5.Click += ItemDeltaButton_Click;
            btnP1.Click += ItemDeltaButton_Click;

            panel.Controls.Add(btnM1);
            panel.Controls.Add(btnP5);
            panel.Controls.Add(btnP1);

            return new ItemRow(index, panel, nameBox, labels);
        }

        private Button MakeDeltaButton(string text, int delta, Point location, Color foreColor)
        {
            return new Button
            {
                Text = text,
                Font = blueP1.Font,
                BackColor = Color.LightGray,
                ForeColor = foreColor,
                Location = location,
                Size = new Size(83, 40)
            };
        }

        private void ItemDeltaButton_Click(object? sender, EventArgs e)
        {
            if (!havebox) return;
            if (sender is not Button btn) return;
            if (btn.Tag is not Tuple<int, int> t) return;

            ApplyItemDelta(t.Item1, t.Item2);
        }

        private void ApplyItemDelta(int itemIndex, int delta)
        {
            if (itemIndex < 0 || itemIndex >= itemRows.Count) return;

            int shiftIndex = GetCurrentShiftIndex();
            var lbl = itemRows[itemIndex].ShiftLabels[shiftIndex];

            int current = 0;
            int.TryParse(lbl.Text, out current);

            int newValue = current + delta;
            int appliedDelta = delta;
            if (newValue < 0)
            {
                appliedDelta = -current;
                newValue = 0;
            }

            lbl.Text = newValue.ToString();

            // update total chart
            chart5.Series[0].Points[shiftIndex].YValues[0] += appliedDelta;
            if (chart5.Series[0].Points[shiftIndex].YValues[0] < 0)
                chart5.Series[0].Points[shiftIndex].YValues[0] = 0;

            UpdateBarColors();
            Updatedgv();
        }

        private int GetCurrentShiftIndex()
        {
            switch (DateTime.Now.Hour + (DateTime.Now.Minute / 60.0))
            {
                case double hour when hour >= shiftStart1 && hour < shiftEnd1:
                    return 0;
                case double hour when hour >= shiftStart2 && hour < shiftEnd2:
                    return 1;
                case double hour when hour >= shiftStart3 && hour < shiftEnd3:
                    return 2;
                case double hour when hour >= shiftStart4 && hour < shiftEnd4:
                    return 3;
                default:
                    return 4;
            }
        }

        private void STHLabel_Click(object sender, EventArgs e)
        {

        }

        private void blue5_Click(object sender, EventArgs e)
        {

        }

        private void label21_Click(object sender, EventArgs e)
        {

        }

        private void panel5_Paint(object sender, PaintEventArgs e)
        {

        }
    }
}
// klass JSON
public class Config
{
    public int LiveScreenGap { get; set; }
}