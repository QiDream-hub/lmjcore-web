#!/usr/bin/env python3
"""
LMJCore Web Server 测试客户端
"""

import requests
import sys

BASE_URL = "http://localhost:8080"

def test_ptr_generation():
    print("Testing pointer generation...")
    
    # 生成对象指针
    resp = requests.post(f"{BASE_URL}/ptr/generate")
    assert resp.status_code == 200
    data = resp.json()
    assert data["code"] == 0
    ptr = data["data"]["ptr"]
    print(f"  Generated object pointer: {ptr}")
    
    # 生成集合指针
    resp = requests.post(f"{BASE_URL}/ptr/generate", headers={"Content-Type": "set"})
    assert resp.status_code == 200
    data = resp.json()
    assert data["code"] == 0
    set_ptr = data["data"]["ptr"]
    print(f"  Generated set pointer: {set_ptr}")
    
    return ptr, set_ptr

def test_obj_operations(ptr):
    print("\nTesting object operations...")
    
    # 创建对象
    resp = requests.post(f"{BASE_URL}/obj")
    assert resp.status_code == 200
    data = resp.json()
    assert data["code"] == 0
    obj_ptr = data["data"]["ptr"]
    print(f"  Created object: {obj_ptr}")
    
    # 写入成员
    resp = requests.put(f"{BASE_URL}/ptr/{obj_ptr}/name", data="Alice")
    assert resp.status_code == 200
    print("  Wrote member: name = Alice")
    
    resp = requests.put(f"{BASE_URL}/ptr/{obj_ptr}/age", data="30")
    assert resp.status_code == 200
    print("  Wrote member: age = 30")
    
    # 读取成员
    resp = requests.get(f"{BASE_URL}/ptr/{obj_ptr}/name")
    assert resp.status_code == 200
    data = resp.json()
    assert data["code"] == 0
    print(f"  Read member name: {data['data']}")
    
    # 获取整个对象
    resp = requests.get(f"{BASE_URL}/ptr/{obj_ptr}")
    assert resp.status_code == 200
    data = resp.json()
    assert data["code"] == 0
    print(f"  Object members: {data['data']['member_count']}")
    
    return obj_ptr

def test_set_operations(set_ptr):
    print("\nTesting set operations...")
    
    # 创建集合
    resp = requests.post(f"{BASE_URL}/set")
    assert resp.status_code == 200
    data = resp.json()
    assert data["code"] == 0
    ptr = data["data"]["ptr"]
    print(f"  Created set: {ptr}")
    
    # 添加元素
    resp = requests.post(f"{BASE_URL}/ptr/{ptr}/add", data="apple")
    assert resp.status_code == 200
    print("  Added: apple")
    
    resp = requests.post(f"{BASE_URL}/ptr/{ptr}/add", data="banana")
    assert resp.status_code == 200
    print("  Added: banana")
    
    # 检查元素
    resp = requests.get(f"{BASE_URL}/ptr/{ptr}/contains", data="apple")
    assert resp.status_code == 200
    data = resp.json()
    assert data["data"]["exists"] == True
    print("  Contains apple: True")
    
    # 获取所有元素
    resp = requests.get(f"{BASE_URL}/ptr/{ptr}/all")
    assert resp.status_code == 200
    data = resp.json()
    print(f"  Set elements count: {data['data']['count']}")
    
    return ptr

def test_chain_query(obj_ptr):
    print("\nTesting chain query...")
    
    # 创建嵌套结构
    # 先创建子对象
    resp = requests.post(f"{BASE_URL}/obj")
    child_ptr = resp.json()["data"]["ptr"]
    
    resp = requests.put(f"{BASE_URL}/ptr/{child_ptr}/first", data="John")
    resp = requests.put(f"{BASE_URL}/ptr/{child_ptr}/last", data="Doe")
    
    # 将子对象链接到父对象
    resp = requests.put(f"{BASE_URL}/ptr/{obj_ptr}/user", data=child_ptr.encode())
    
    # 链式查询
    resp = requests.get(f"{BASE_URL}/ptr/{obj_ptr}/user.first")
    assert resp.status_code == 200
    data = resp.json()
    print(f"  Chain query user.first: {data['data']}")
    
    resp = requests.get(f"{BASE_URL}/ptr/{obj_ptr}/user.last")
    assert resp.status_code == 200
    data = resp.json()
    print(f"  Chain query user.last: {data['data']}")

def test_audit(obj_ptr):
    print("\nTesting audit...")
    
    resp = requests.get(f"{BASE_URL}/audit/{obj_ptr}")
    assert resp.status_code == 200
    data = resp.json()
    print(f"  Audit result: {data['data']['integrity']}")

def main():
    print("LMJCore Web Server Test Client")
    print("===============================")
    print(f"Target: {BASE_URL}")
    
    try:
        # 测试健康检查
        resp = requests.get(f"{BASE_URL}/")
        print(f"Server status: {resp.status_code}")
        
        # 运行测试
        ptr, set_ptr = test_ptr_generation()
        obj_ptr = test_obj_operations(ptr)
        test_set_operations(set_ptr)
        test_chain_query(obj_ptr)
        test_audit(obj_ptr)
        
        print("\nAll tests passed!")
        
    except requests.exceptions.ConnectionError:
        print("ERROR: Cannot connect to server. Make sure it's running on port 8080")
        sys.exit(1)
    except AssertionError as e:
        print(f"Test failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()