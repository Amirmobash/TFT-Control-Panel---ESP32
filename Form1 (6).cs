
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
        // old fixed 4-
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
