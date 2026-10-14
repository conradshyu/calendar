# Generates ribbon images (32bpp ARGB BMP, 16x16 and 32x32) and the application icon
# from Windows' built-in "Segoe MDL2 Assets" icon font. Run from the project folder:
#   powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot "..\src\res"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# name = glyph codepoint, color
$icons = [ordered]@{
  newevent=@(0xE787,'#0067C0'); newmeeting=@(0xE8FA,'#0067C0'); newtask=@(0xE9D5,'#107C10'); open=@(0xE8E5,'#5C5C5C')
  delete=@(0xE74D,'#C42B1C'); categorize=@(0xE8EC,'#CA5010'); today=@(0xE8D1,'#0067C0'); prev=@(0xE76B,'#404040')
  next=@(0xE76C,'#404040'); goto=@(0xE8AD,'#0067C0'); viewday=@(0xE8BF,'#0067C0'); viewweek=@(0xE8C0,'#0067C0')
  viewmonth=@(0xE787,'#0067C0'); viewyear=@(0xE8A9,'#0067C0'); viewagenda=@(0xE8FD,'#0067C0'); search=@(0xE721,'#404040')
  attendees=@(0xE716,'#0067C0'); sendinvites=@(0xE724,'#0067C0'); rsvp=@(0xE8F5,'#107C10'); invitations=@(0xE715,'#0067C0')
  accept=@(0xE8FB,'#107C10'); tentative=@(0xE9CE,'#8764B8'); decline=@(0xE711,'#C42B1C'); propose=@(0xE823,'#CA5010')
  teams=@(0xE714,'#5B5FC7'); zoom=@(0xE714,'#0B5CFF'); meet=@(0xE714,'#00897B'); jitsi=@(0xE714,'#1D76BA'); join=@(0xE768,'#107C10')
  importinvite=@(0xE8CA,'#0067C0'); calendars=@(0xE787,'#8764B8'); newcalendar=@(0xE710,'#8764B8'); sharing=@(0xE72D,'#0067C0')
  openshared=@(0xE838,'#CA5010'); categories=@(0xE790,'#CA5010'); accounts=@(0xE753,'#0067C0'); sync=@(0xE895,'#107C10')
  import=@(0xE896,'#0067C0'); export=@(0xE898,'#0067C0'); timezones=@(0xE909,'#0067C0'); autotz=@(0xE707,'#C42B1C')
  reminders=@(0xE7ED,'#CA5010'); notifysettings=@(0xE7E7,'#5C5C5C'); testnotify=@(0xEA8F,'#CA5010'); focus=@(0xE708,'#8764B8')
  ooo=@(0xE709,'#0067C0'); focussettings=@(0xE713,'#8764B8'); tasks=@(0xE9D5,'#107C10'); showcompleted=@(0xE930,'#107C10')
  options=@(0xE713,'#5C5C5C'); exit=@(0xE7E8,'#C42B1C'); about=@(0xE897,'#0067C0')
}

function Write-Bmp32([System.Drawing.Bitmap]$bmp, [string]$path) {
  $w = $bmp.Width; $h = $bmp.Height
  $rect = New-Object System.Drawing.Rectangle 0,0,$w,$h
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $data.Stride; $bytes = New-Object byte[] ($stride * $h)
  [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length); $bmp.UnlockBits($data)
  $ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter $ms
  $img = $w * 4 * $h
  $bw.Write([uint16]0x4D42); $bw.Write([uint32](54 + $img)); $bw.Write([uint32]0); $bw.Write([uint32]54)
  $bw.Write([uint32]40); $bw.Write([int32]$w); $bw.Write([int32]$h); $bw.Write([uint16]1); $bw.Write([uint16]32)
  $bw.Write([uint32]0); $bw.Write([uint32]$img); $bw.Write([int32]2835); $bw.Write([int32]2835); $bw.Write([uint32]0); $bw.Write([uint32]0)
  for ($y = $h - 1; $y -ge 0; $y--) { $bw.Write($bytes, $y * $stride, $w * 4) }
  $bw.Flush(); [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

function Render([int]$cp, [string]$hex, [int]$size) {
  $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $gr = [System.Drawing.Graphics]::FromImage($bmp)
  $gr.Clear([System.Drawing.Color]::Transparent)
  $gr.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $font = New-Object System.Drawing.Font 'Segoe MDL2 Assets', ($size * 0.70), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
  $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml($hex))
  $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
  $gr.DrawString([string][char]$cp, $font, $brush, (New-Object System.Drawing.RectangleF 0, 1, $size, $size), $fmt)
  $gr.Dispose(); return $bmp
}

foreach ($k in $icons.Keys) {
  $v = $icons[$k]
  foreach ($s in 16, 32) { $b = Render $v[0] $v[1] $s; Write-Bmp32 $b (Join-Path $out "$k$s.bmp"); $b.Dispose() }
}

# Application icon: white calendar glyph on a blue rounded tile, PNG-compressed ICO
$sizes = 16, 24, 32, 48, 64, 256; $pngs = @()
foreach ($s in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $gr = [System.Drawing.Graphics]::FromImage($bmp); $gr.SmoothingMode = 'AntiAlias'; $gr.Clear([System.Drawing.Color]::Transparent)
  $gr.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $r = [Math]::Max(2, [int]($s / 6)); $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddArc(0, 0, $r * 2, $r * 2, 180, 90); $path.AddArc($s - 1 - $r * 2, 0, $r * 2, $r * 2, 270, 90)
  $path.AddArc($s - 1 - $r * 2, $s - 1 - $r * 2, $r * 2, $r * 2, 0, 90); $path.AddArc(0, $s - 1 - $r * 2, $r * 2, $r * 2, 90, 90); $path.CloseFigure()
  $gr.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#0067C0'))), $path)
  $font = New-Object System.Drawing.Font 'Segoe MDL2 Assets', ($s * 0.62), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
  $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
  $gr.DrawString([string][char]0xE787, $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, ($s * 0.04), $s, $s), $fmt)
  $gr.Dispose(); $ms = New-Object IO.MemoryStream; $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $pngs += , $ms.ToArray(); $bmp.Dispose()
}
$ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter $ms
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]; $d = if ($s -ge 256) { 0 } else { $s }
  $bw.Write([byte]$d); $bw.Write([byte]$d); $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([uint16]1); $bw.Write([uint16]32)
  $bw.Write([uint32]$pngs[$i].Length); $bw.Write([uint32]$offset); $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Flush(); [IO.File]::WriteAllBytes((Join-Path $out "app.ico"), $ms.ToArray())
Write-Host "Generated $($icons.Count * 2) ribbon bitmaps and app.ico in $out"
