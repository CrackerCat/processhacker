using System;
using System.Collections.Generic;
using System.Text;
using System.Windows.Forms;
using System.Security.Permissions;
using System.Drawing;

namespace Aga.Controls.Tree
{
	internal class ResizeColumnState: ColumnState
	{
		private Point _initLocation;
		private int _initWidth;

		public ResizeColumnState(TreeViewAdv tree, TreeColumn column, Point p)
			: base(tree, column)
		{
			_initLocation = p;
			_initWidth = column.Width;
		}

		public override void KeyDown(KeyEventArgs args)
		{
			args.Handled = true;
			if (args.KeyCode == Keys.Escape)
				FinishResize();
		}

		public override void MouseDown(TreeNodeAdvMouseEventArgs args)
		{
		}

		public override void MouseUp(TreeNodeAdvMouseEventArgs args)
		{
			FinishResize();
		}

		private void FinishResize()
		{
			this.Tree.ChangeInput();
			this.Tree.FullUpdate();
			this.Tree.OnColumnWidthChanged(this.Column);
		}

        public override bool MouseMove(MouseEventArgs args)
        {
			this.Column.Width = _initWidth + args.Location.X - _initLocation.X;
            this.Tree.UpdateView();
            this.Tree.Invalidate();
            return true;
        }
	}
}
