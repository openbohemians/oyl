// Prints sigs.k8s.io/yaml's YAMLToJSON for each input file, or its error.
package main

import (
	"fmt"
	"os"

	"sigs.k8s.io/yaml"
)

func main() {
	for _, path := range os.Args[1:] {
		in, err := os.ReadFile(path)
		if err != nil {
			fmt.Println("READ ERROR", err)
			continue
		}
		out, err := yaml.YAMLToJSON(in)
		if err != nil {
			fmt.Printf("ERROR %v\n", err)
		} else {
			fmt.Printf("%s\n", out)
		}
	}
}
