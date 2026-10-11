// Prints go-yaml v2's tag and value for each input line: tag \t kind \t value
package main

import (
	"bufio"
	"fmt"
	"math"
	"os"
	"time"

	yaml "go.yaml.in/yaml/v2"
)

func main() {
	in := bufio.NewScanner(os.Stdin)
	in.Buffer(make([]byte, 1<<20), 1<<20)
	out := bufio.NewWriter(os.Stdout)
	defer out.Flush()
	for in.Scan() {
		tag, v := yaml.Resolve(in.Text())
		switch x := v.(type) {
		case nil:
			fmt.Fprintf(out, "%s\tnull\t\n", tag)
		case bool:
			fmt.Fprintf(out, "%s\tbool\t%v\n", tag, x)
		case int:
			fmt.Fprintf(out, "%s\tint\t%d\n", tag, x)
		case int64:
			fmt.Fprintf(out, "%s\tint\t%d\n", tag, x)
		case uint64:
			fmt.Fprintf(out, "%s\tuint\t%d\n", tag, x)
		case float64:
			if math.IsNaN(x) {
				fmt.Fprintf(out, "%s\tfloat\tnan\n", tag)
			} else {
				fmt.Fprintf(out, "%s\tfloat\t%016x\n", tag, math.Float64bits(x))
			}
		case string:
			fmt.Fprintf(out, "%s\tstr\t\n", tag)
		case time.Time:
			fmt.Fprintf(out, "%s\tstr\t\n", tag)
		default:
			fmt.Fprintf(out, "%s\t?%T\t\n", tag, v)
		}
	}
}
