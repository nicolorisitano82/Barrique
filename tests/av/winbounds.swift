// Print the on-screen bounds, in points, of the windows of processes whose
// name contains the argument (default "wine"): "<owner> <title> x y w h".
// CGWindowList bounds need no screen recording permission (titles may be
// empty without it). Used by the phase 5 DPI tests (dpitest.exe).
import CoreGraphics
let match = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "wine"
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
for w in list {
    let owner = w[kCGWindowOwnerName as String] as? String ?? ""
    guard owner.lowercased().contains(match) else { continue }
    let name = w[kCGWindowName as String] as? String ?? ""
    var r = CGRect.zero
    if let b = w[kCGWindowBounds as String] { r = CGRect(dictionaryRepresentation: b as! CFDictionary) ?? .zero }
    print(owner, "'\(name)'", Int(r.origin.x), Int(r.origin.y), Int(r.width), Int(r.height))
}
