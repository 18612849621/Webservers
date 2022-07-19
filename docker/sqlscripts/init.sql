use yourdb;
create table user (
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;

insert into user(username, passwd) values ('pan', 'woaini123');