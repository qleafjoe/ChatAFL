import os
import json
import requests

def test_minimax_prompt():
    url = os.getenv("LLM_URL", "https://api.minimaxi.com/v1/text/chatcompletion_v2")
    token = os.getenv("LLM_TOKEN", "sk-cp-EK3rwNPjpttunXcODVKSpsvJh4dySqRdtbgbjcmLxdlSHRyoIuWJzFPXFUr8I8rponL4y-xwRMMcO3eodW7dwfO2hqL3G6cCQBtIufVHuRX11_JV1YK5YFs")
    model = os.getenv("LLM_MODEL", "MiniMax-M2.7")

    # The new prompt template we implemented
    new_constraint = "(System constraint: Output exactly ONE complete client request message. MUST include headers and MUST end with \\r\\n\\r\\n. NO markdown, NO formatting, NO explanations.)"
    
    # Context from the real failed run (prompt-61)
    history = "TEARDOWN rtsp://127.0.0.1:8554/wavAudioTest/ RTSP/1.0\\r\\nCSeq: 5\\r\\nUser-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)\\r\\nSession: 000022B8\\r\\n\\r\\nRTSP/1.0 454 Session Not Found\\r\\nCSeq: 5\\r\\nDate: Tue, Apr 21 2026 00:47:04 GMT\\r\\n\\r\\n"
    
    user_content = f"In the RTSP protocol, the communication history between the RTSP client and the RTSP server is as follows. The next proper client request that can affect the server's state are:\\n\\nCommunication History:\\n\\\"\\\"\\\"\\n{history}\\\"\n{new_constraint}"

    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": "You are a network protocol expert assistant. Output ONLY the raw required protocol command."},
            {"role": "user", "content": user_content}
        ],
        "temperature": 0.1
    }

    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json"
    }

    print("Sending simulation prompt to MiniMax...")
    response = requests.post(url, headers=headers, json=payload)
    
    if response.status_code == 200:
        result = response.json()
        content = result['choices'][0]['message']['content']
        print("\n--- RAW RESPONSE ---")
        print(repr(content))
        print("--- END ---")
        
        if "\\r\\n\\r\\n" in content or "\r\n\r\n" in content:
            print("\nResult: SUCCESS! Response contains required termination markers.")
        else:
            print("\nResult: FAILURE. Response is still incomplete.")
    else:
        print(f"Error: {response.status_code}")
        print(response.text)

if __name__ == "__main__":
    test_minimax_prompt()
