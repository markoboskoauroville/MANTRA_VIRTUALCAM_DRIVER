// Runs on the GitHub runner (swift icon/make_icon.swift): draws the driver icon and writes
// icon/MantraVirtualCam.icns. Six letters VIR/CAM over one microphone, Audio teal (modules/six-letter-icon.md).
import AppKit

func render(_ px: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let s = CGFloat(px), tile = s * 824 / 1024, o = (s - tile) / 2
    let r = NSRect(x: o, y: o, width: tile, height: tile)
    let path = NSBezierPath(roundedRect: r, xRadius: tile * 0.225, yRadius: tile * 0.225)
    NSGradient(starting: NSColor(calibratedRed: 0.10, green: 0.62, blue: 0.62, alpha: 1),
               ending: NSColor(calibratedRed: 0.03, green: 0.36, blue: 0.38, alpha: 1))!.draw(in: path, angle: -90)
    if let mic = NSImage(systemSymbolName: "mic.fill", accessibilityDescription: nil)?
        .withSymbolConfiguration(.init(pointSize: tile * 0.62, weight: .regular)) {
        let tinted = NSImage(size: mic.size, flipped: false) { rect in
            mic.draw(in: rect); NSColor(white: 1, alpha: 0.46).set(); rect.fill(using: .sourceAtop); return true }
        let w = mic.size.width, h = mic.size.height
        tinted.draw(in: NSRect(x: (s - w) / 2, y: (s - h) / 2, width: w, height: h))
    }
    var font = NSFont.systemFont(ofSize: tile * 0.27, weight: .black)
    if let d = font.fontDescriptor.withDesign(.rounded) { font = NSFont(descriptor: d, size: tile * 0.27) ?? font }
    let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.white]
    for (i, word) in ["VIR", "CAM"].enumerated() {
        let str = NSAttributedString(string: word, attributes: attrs), sz = str.size()
        let y = s / 2 + (i == 0 ? tile * 0.02 : -sz.height - tile * 0.02)
        str.draw(at: NSPoint(x: (s - sz.width) / 2, y: y))
    }
    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

let dir = URL(fileURLWithPath: "icon/MantraVirtualCam.iconset")
try? FileManager.default.removeItem(at: dir)
try! FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    try! render(base).write(to: dir.appendingPathComponent("icon_\(base)x\(base).png"))
    try! render(base * 2).write(to: dir.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}
try! render(1024).write(to: URL(fileURLWithPath: "icon/six_1024.png"))
let p = Process(); p.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
p.arguments = ["-c", "icns", dir.path, "-o", "icon/MantraVirtualCam.icns"]
try! p.run(); p.waitUntilExit()
precondition(p.terminationStatus == 0, "iconutil failed")
print("icon/MantraVirtualCam.icns written")
