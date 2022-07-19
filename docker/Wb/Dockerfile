FROM ubuntu:20.04

ENV USER=pan
ENV PASSWD=woaini123
ENV WORKDIR=projects
ENV DEBIAN_FRONTEND=noninteractive

# 安装开发环境必要的包
RUN apt-get update \
  && apt-get install -y ssh openssh-server build-essential \
    gcc g++ gdb gdbserver cmake \
    # 配置vim
    vim \
    iputils-ping \
    # 无需libboost可以去掉下一行
    libboost-dev \
    # net-tools 提供了ifconfig
    net-tools tar rsync \
    # libmysqlclient-dev
    libmysqlclient-dev \
    # # 无需python3可以去掉下一行
    # python3 python3-pip \
    sudo git\
  && apt-get clean

# 添加用户并配置密码
RUN useradd -m ${USER} && yes ${PASSWD} | passwd ${USER}

# 赋予sudo权限并允许无密码sudo
RUN echo ${USER}' ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
RUN chmod 644 /etc/sudoers


CMD ["/usr/sbin/sshd", "-D"]

USER ${USER}
RUN mkdir -p /home/${USER}/${WORKDIR}/
ENV LC_ALL C.UTF-8