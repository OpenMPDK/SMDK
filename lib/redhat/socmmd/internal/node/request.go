package node

import (
	"crypto/tls"
	"fmt"
	"io"
	"net/http"
	"strings"
)

func Get(urlPath string) ([]byte, error) {
	if strings.HasPrefix(urlPath, "https:") {
		return secureRequest(urlPath)
	}
	return request(urlPath)
}

func request(urlPath string) ([]byte, error) {

	resp, err := http.Get(urlPath)

	if err != nil {
		return nil, fmt.Errorf("http error : %w", err)
	}

	defer resp.Body.Close()

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body error : %w", err)
	}

	return data, err

}

func secureRequest(urlPath string) ([]byte, error) {

	tr := &http.Transport{
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: true,
		},
	}

	client := &http.Client{Transport: tr}

	resp, err := client.Get(urlPath)

	if err != nil {
		return nil, fmt.Errorf("https error : %w", err)
	}

	defer resp.Body.Close()

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body error : %w", err)
	}

	return data, err

}
