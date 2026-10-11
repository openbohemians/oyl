package yaml

// Resolve exposes resolve for the Oyl oracle (test only).
func Resolve(s string) (string, interface{}) { return resolve("", s) }
