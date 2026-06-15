import paramiko
import sys
import xml.etree.ElementTree as ET

def parse_xml(file_path, element, attribute_name, attribute_value, name):
    # 解析XML文件
    tree = ET.parse(file_path)
    elements = tree.findall(element)
    for elem in elements:
        if elem.get(attribute_name) == name:
            value = elem.get(attribute_value)
            return value
    return None

def ssh_login(hostname, username, password):
    try:
        # 创建SSH客户端对象
        client = paramiko.SSHClient()

        # 设置自动添加主机密钥
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        # 连接SSH服务器
        client.connect(hostname, username=username, password=password)

        print(f"成功连接到 {hostname}")
        # 在连接后执行命令
        stdin, stdout, stderr = client.exec_command('ls -l')
        response = stdout.read() + stderr.read()
        if response:
            print("Received response from SSH server:", response.decode('utf-8'))
        else:
            print("No response from SSH server.")
            return False

        # 断开连接
        client.close()

        return True  # 成功连接返回True

    except paramiko.AuthenticationException:
        print(f"认证失败，请检查用户名和密码是否正确")
        sys.exit(1)  # 认证失败时返回非零退出码，终止 Peach 测试
    except Exception as e:
        print(f"连接发生错误: {str(e)}")
        sys.exit(2)  # 其他连接错误时返回非零退出码

if __name__ == "__main__":
    # 读取配置文件中的 SSH 连接参数
    path_to_config = 'D:/networkprogram/MPLS/11.23ssh第三版/9.22ssh第三版/SSHv2/SSHv2_PIT文件及参数设置/sshv2.xml.config'
    hostname = parse_xml(path_to_config, './/Define', 'key', 'value', 'TargetIPv4')
    username = parse_xml(path_to_config, './/Define', 'key', 'value', 'username')
    password = parse_xml(path_to_config, './/Define', 'key', 'value', 'password')

    # 执行SSH登录
    result = ssh_login(hostname, username, password)
    if not result:
        sys.exit(1)  # 登录失败返回非零退出码
    sys.exit(0)  # 成功返回0，表示测试通过
