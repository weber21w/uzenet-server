import email
import imaplib
import datetime
import sys

email_user = sys.argv[1:]
email_pass = sys.argv[2:]
email_server = sys.argv[3:]

mail = imaplib.IMAP4_SSL(email_server)
mail.login(email_user, email_pass)
mail.select('inbox')

# print(email_user)
# print(email_pass)
# we'll search using the ALL criteria to retrieve
# every message inside the inbox
# it will return with its status and a list of ids
#since_date = (datetime.date.today() - datetime.timedelta(2)).strftime("%d-%b-%Y")
#'(SENTSINCE {0})'.format(since_date)
status, data = mail.search(None, '(UNSEEN)')
#status, date = mail.search(None, 'ALL', '(SENSINCE {0})'.format(since_date))


# the list returned is a list of bytes separated
# by white spaces on this format: [b'1 2 3', b'4 5 6']
# so, to separate it first we create an empty list
mail_ids = []
# then we go through the list splitting its blocks
# of bytes and appending to the mail_ids list
for block in data:
    # the split function called without parameter
    # transforms the text or bytes into a list using
    # as separator the white spaces:
    # b'1 2 3'.split() => [b'1', b'2', b'3']
    mail_ids += block.split()

# now for every id we'll fetch the email
# to extract its content


for i in mail_ids:


    # the fetch function fetch the email given its id
    # and format that you want the message to be
    status, data = mail.fetch(i, '(RFC822)')
    break ##hack, just grab a single email


    # the content data at the '(RFC822)' format comes on
    # a list with a tuple with header, content, and the closing
    # byte b')'

mail_count = 0
attachment_count = 0
for response_part in data:
        # so if its a tuple...
    if isinstance(response_part, tuple):
            # we go for the content at its second element
            # skipping the header at the first and the closing
            # at the third
        message = email.message_from_bytes(response_part[1])

            # with the content we can extract the info about
            # who sent the message and its subject
        mail_from = message['from']
        mail_subject = message['subject']

            # then for the text we have a little more work to do
            # because it can be in plain text or multipart
            # if its not plain text we need to separate the message
            # from its annexes to get the text
        if message.is_multipart():
            mail_content = ''



            for part in message.walk():
                ctype = part.get_content_type()
                cdispo = str(part.get('Content-Disposition'))

                # skip any text/plain (txt) attachments
                if ctype == 'text/plain' and 'attachment' not in cdispo:
                    body = part.get_payload(decode=True)  # decode
                    print(f'<from>{mail_from}</from>')
                    print(f'<subject>{mail_subject}</subject>')
                    print(f'<body>{body.decode("UTF-8")}</body>')
                    #output=str(body)
                    #print(output.replace("\n","\\n"))
                    ##return #################################HACK, JUST GET ONE EMAIL
                else:
                    if ctype in ['image/jpeg', 'image/png', 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet']:
                        open('attachments/' + part.get_filename(), 'wb').write(part.get_payload(decode=True))
                        attachment_count = attachment_count + 1
                # on multipart we have the text message and
                # another things like annex, and html version
                # of the message, in that case we loop through
                # the email payload
            #for part in message.get_payload():
                    # if the content type is text/plain
                    # we extract it
            #    if part.get_content_type() == 'text/plain':
            #        mail_content += part.get_payload(decode=True)
        else:
                # if the message isn't multipart, just extract it
            mail_content = message.get_payload(decode=True)

            # and then let's show its result
        #print(f'From: {mail_from}')
       # print(f'<subject>{mail_subject}</subject>')
       # print(f'<body>{mail_content}</body>')
        #return##### HACK HACK
        mail_count = mail_count + 1


if mail_count > 0:
    print(f'!!!!!!!!<EMAIL-RETRIEVED>!!!!!!!!')
else:
    print(f'!!!!!!!!<NO-EMAIL-RETRIEVED>!!!!!!!!')

##this is hacked to retrieve exactly 1 unread email...for various reasons this is easier...
  
