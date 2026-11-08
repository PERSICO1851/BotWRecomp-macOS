import AppKit

guard CommandLine.arguments.count == 2 else {
    fatalError("usage: swift make_macos_icon.swift output.png")
}

let size = 1024
let bitmap = NSBitmapImageRep(
    bitmapDataPlanes: nil,
    pixelsWide: size,
    pixelsHigh: size,
    bitsPerSample: 8,
    samplesPerPixel: 4,
    hasAlpha: true,
    isPlanar: false,
    colorSpaceName: .deviceRGB,
    bytesPerRow: 0,
    bitsPerPixel: 0
)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
NSGraphicsContext.current?.imageInterpolation = .high

NSColor(calibratedRed: 0.035, green: 0.12, blue: 0.105, alpha: 1).setFill()
NSBezierPath(rect: NSRect(x: 0, y: 0, width: size, height: size)).fill()

let gold = NSColor(calibratedRed: 0.96, green: 0.77, blue: 0.32, alpha: 1)
let background = NSColor(calibratedRed: 0.035, green: 0.12, blue: 0.105, alpha: 1)
let triangles: [[NSPoint]] = [
    [NSPoint(x: 512, y: 790), NSPoint(x: 348, y: 506), NSPoint(x: 676, y: 506)],
    [NSPoint(x: 348, y: 488), NSPoint(x: 184, y: 204), NSPoint(x: 512, y: 204)],
    [NSPoint(x: 676, y: 488), NSPoint(x: 512, y: 204), NSPoint(x: 840, y: 204)],
]

for vertices in triangles {
    let path = NSBezierPath()
    path.lineJoinStyle = .round
    path.lineWidth = 16
    path.move(to: vertices[0])
    path.line(to: vertices[1])
    path.line(to: vertices[2])
    path.close()
    gold.setFill()
    path.fill()
    background.setStroke()
    path.stroke()
}

NSGraphicsContext.restoreGraphicsState()
let png = bitmap.representation(using: .png, properties: [:])!
try png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
