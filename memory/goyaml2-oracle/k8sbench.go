// Times sigs.k8s.io/yaml's YAMLToJSON over the files named on stdin.
package main

import (
	"bufio"
	"fmt"
	"os"
	"time"

	"sigs.k8s.io/yaml"
)

func main() {
	var docs [][]byte
	total := 0
	in := bufio.NewScanner(os.Stdin)
	for in.Scan() {
		b, _ := os.ReadFile(in.Text())
		docs = append(docs, b)
		total += len(b)
	}
	best := time.Hour
	for r := 0; r < 5; r++ {
		t0 := time.Now()
		for _, d := range docs {
			yaml.YAMLToJSON(d)
		}
		if t := time.Since(t0); t < best {
			best = t
		}
	}
	fmt.Printf("sigs.k8s.io/yaml: %d docs, %.1f MB, %.1f ms, %.1f MB/s\n", len(docs), float64(total)/1e6,
		float64(best.Microseconds())/1000, float64(total)/1e6/best.Seconds())
}
